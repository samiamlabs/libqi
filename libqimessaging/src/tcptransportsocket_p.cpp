/*
**  Copyright (C) 2012 Aldebaran Robotics
**  See COPYING for the license
*/
#include <iostream>
#include <cstring>
#include <map>

#ifdef ANDROID
#include <linux/in.h> // for  IPPROTO_TCP
#endif

#include <event2/util.h>
#include <event2/event.h>
#include <event2/buffer.h>
#include <event2/bufferevent.h>
#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "src/tcptransportsocket.hpp"
#include "src/transportsocket_p.hpp"
#include "src/tcptransportsocket_p.hpp"
#include "src/message_p.hpp"
#include "src/buffer_p.hpp"
#include "src/eventloop_p.hpp"

#include <qi/log.hpp>
#include <qi/types.hpp>
#include <qimessaging/session.hpp>
#include <qimessaging/message.hpp>
#include <qimessaging/datastream.hpp>
#include <qimessaging/buffer.hpp>
#include <qi/eventloop.hpp>

#define MAX_LINE 16384

namespace qi
{

  // Forward libevent C callback to C++
  void readcb(struct bufferevent *bev, void *context)
  {
    TcpTransportSocketPrivate *tc = static_cast<TcpTransportSocketPrivate*>(context);
    tc->onRead();
  }

  void eventcb(struct bufferevent *bev, short error, void *context)
  {
    TcpTransportSocketPrivate *tc = static_cast<TcpTransportSocketPrivate*>(context);
    tc->onEvent(error);
  }


  //#### TransportSocketLibEvent


  TcpTransportSocketPrivate::TcpTransportSocketPrivate(TransportSocket *self, EventLoop* eventLoop)
    : TransportSocketPrivate(self, eventLoop)
    , _bev(NULL)
    , _readHdr(true)
    , _msg(0)
    , _connecting(false)
  {
  }

  TcpTransportSocketPrivate::TcpTransportSocketPrivate(TransportSocket *self, int fd, EventLoop* eventLoop)
    : TransportSocketPrivate(self, eventLoop)
    , _bev(NULL)
    , _readHdr(true)
    , _msg(0)
    , _connecting(false)
    , _fd(fd)
  {
    _connected = true;
  }

  void TcpTransportSocketPrivate::startReading()
  {
    if (_bev)
      return;
    if (!_eventLoop->isInEventLoopThread())
    {
      _eventLoop->asyncCall(0,
        boost::bind(&TcpTransportSocketPrivate::startReading, this));
      return;
    }
    struct event_base *base = static_cast<event_base *>(_eventLoop->_p->getEventBase());
    _bev = bufferevent_socket_new(base, _fd, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(_bev, ::qi::readcb, 0, ::qi::eventcb, this);
    bufferevent_setwatermark(_bev, EV_WRITE, 0, MAX_LINE);
    bufferevent_enable(_bev, EV_READ|EV_WRITE);
  }

  TcpTransportSocketPrivate::~TcpTransportSocketPrivate()
  {
    delete _msg;
    disconnect();
  }

  void TcpTransportSocketPrivate::onRead()
  {
    if (!_bev)
      return;

    struct evbuffer *input = bufferevent_get_input(_bev);
    unsigned int     magicValue = qi::MessagePrivate::magic;

    while (true)
    {
      if (_msg == NULL)
      {
        _msg = new qi::Message();
        _readHdr = true;
      }

      if (_readHdr)
      {
        if (evbuffer_get_length(input) < sizeof(MessagePrivate::MessageHeader))
          return;

        struct evbuffer_ptr p;
        evbuffer_ptr_set(input, &p, 0, EVBUFFER_PTR_SET);
        // search a messageMagic number
        p = evbuffer_search(input, (const char *)&magicValue, sizeof(qi::uint32_t), &p);
        if (p.pos < 0) {
          qiLogWarning("qimessaging.TransportSocketLibevent") << "No magic found in the message. Waiting for more data.";
          return;
        }
        // Drain to the magic
        evbuffer_drain(input, p.pos);
        // Get the message header
        // there is a copy here.
        evbuffer_copyout(input, _msg->_p->getHeader(), sizeof(MessagePrivate::MessageHeader));
        // check if the msg is valid
        if (!_msg->isValid())
        {
          qiLogError("qimessaging.TransportSocketLibevent") << "Message received is invalid! Try to find a new one.";
          // only drop the magic and restart scanning
          evbuffer_drain(input, sizeof(qi::uint32_t));
          return;
        }
        // header is valid, next step get all the buffer
        // remove the header from the evbuffer
        evbuffer_drain(input, sizeof(MessagePrivate::MessageHeader));
        _readHdr = false;
      }

      if (evbuffer_get_length(input) < static_cast<MessagePrivate::MessageHeader*>(_msg->_p->getHeader())->size)
        return;

      /* we have to let the Buffer know we are going to push some data inside */
      qi::Buffer buf;
      buf.reserve(static_cast<MessagePrivate::MessageHeader*>(_msg->_p->getHeader())->size);
      _msg->setBuffer(buf);

      evbuffer_remove(input, buf.data(), buf.size());
      _self->messageReady(*_msg);
      _dispatcher.dispatch(*_msg);
      delete _msg;
      _msg = 0;
    }
  }

  void TcpTransportSocketPrivate::onEvent(short events)
  {
    if (!_bev)
      return;

    if (events & BEV_EVENT_CONNECTED)
    {
      _connected = true;
      _connecting = false;
      _connectPromise.setValue(true);
      _self->connected();
    }
    else if ((events & BEV_EVENT_EOF) || (events & BEV_EVENT_ERROR))
    {
      if (events & BEV_EVENT_ERROR)
        _status = errno;
      if (_status)
        qiLogVerbose("qimessaging.TransportSocketLibevent")  << "socket terminate (" << errno << "): " << strerror(errno) << std::endl;
      disconnect();
    }
    else if (events & BEV_EVENT_TIMEOUT)
    {
      // must be a timeout event handle, handle it
      qiLogError("qimessaging.TransportSocketLibevent")  << "must be a timeout event handle, handle it" << std::endl;
    }
  }

  void TcpTransportSocketPrivate::onBufferSent(const void *QI_UNUSED(data),
                                        size_t QI_UNUSED(datalen),
                                        void *buffer)
  {
    Buffer *b = static_cast<Buffer *>(buffer);
    delete b;
  }

  void TcpTransportSocketPrivate::onMessageSent(const void *QI_UNUSED(data),
                                         size_t QI_UNUSED(datalen),
                                         void *msg)
  {
    Message *m = static_cast<Message *>(msg);
    delete m;
  }

  qi::FutureSync<bool> TcpTransportSocketPrivate::connect(const qi::Url &url)
  {
    if (_eventLoop->isInEventLoopThread())
      connect_(url);
    else
      _eventLoop->asyncCall(0,
        boost::bind(&TcpTransportSocketPrivate::connect_, this, url));
    return _connectPromise.future();
  }

  void TcpTransportSocketPrivate::connect_(const qi::Url &url)
  {
    if (_connected) {
      qiLogError("qimessaging.TransportSocketLibevent") << "socket is already connected.";
      _connectPromise.setValue(false);
      return;
    }
    _url = url;
    _connectPromise.reset();
    _disconnectPromise.reset();
    _status = 0;
    const std::string      &address = _url.host();
    struct evutil_addrinfo *ai = NULL;
    struct evutil_addrinfo  hint;
    char                    portbuf[10];

    if (_url.port() == 0) {
      qiLogError("qimessaging.TransportSocket") << "Error try to connect to a bad address: " << _url.str();
      _connectPromise.setError("Bad address " + _url.str());
      return;
    }
    qiLogVerbose("qimessaging.transportsocket.connect") << "Trying to connect to " << _url.host() << ":" << _url.port();

    _bev = bufferevent_socket_new(_eventLoop->_p->getEventBase(), -1, BEV_OPT_CLOSE_ON_FREE);
    bufferevent_setcb(_bev, ::qi::readcb, 0, ::qi::eventcb, this);
    bufferevent_setwatermark(_bev, EV_WRITE, 0, MAX_LINE);
    bufferevent_enable(_bev, EV_READ|EV_WRITE);

    evutil_snprintf(portbuf, sizeof(portbuf), "%d", _url.port());
    memset(&hint, 0, sizeof(hint));
    hint.ai_family   = AF_UNSPEC;
    hint.ai_protocol = IPPROTO_TCP;
    hint.ai_socktype = SOCK_STREAM;
    int err = evutil_getaddrinfo(address.c_str(), portbuf, &hint, &ai);
    if (err != 0)
    {
      qiLogError("qimessaging.TransportSocketLibEvent") << "Cannot resolve dns (" << address << ")";
      _connectPromise.setValue(false);
      return;
    }

    _connecting = true;
    int result = bufferevent_socket_connect(_bev, ai->ai_addr, ai->ai_addrlen);

    evutil_freeaddrinfo(ai);
    if (result) {
      _connectPromise.setValue(false);
      _connecting = false;
    }
  }

  qi::FutureSync<void> TcpTransportSocketPrivate::disconnect()
  {
    //do nothing if already disconnected and not connecting
    if (!_connected && !_connecting) {
      _disconnectPromise.setValue(0);
      return _disconnectPromise.future();
    }

    /*
     * Currently (as of Libevent 2.0.5-beta), bufferevent_flush() is only
     * implemented for some bufferevent types. In particular, socket-based
     * bufferevents dont have it.
     * (http://www.wangafu.net/~nickm/libevent-book/Ref6_bufferevent.html)
     * :'(
     */
    //bufferevent_flush(bev, EV_WRITE, BEV_NORMAL);
    // Must be in network thread due to a libevent deadlock bug.
    if (_bev) {
      bufferevent_setcb(_bev, 0, 0, 0, 0);
      bufferevent_free(_bev);
      _bev = NULL;
    }

    _connected = false;
    _self->disconnected(_status);
    _disconnectPromise.setValue(0);
    _connectPromise.setError(strerror(_status));
    return _disconnectPromise.future();
  }

  bool TcpTransportSocketPrivate::send(const qi::Message &msg)
  {
    if (_eventLoop->isInEventLoopThread())
      return send_(msg, false);
    else
    {
      qi::Message* m = new qi::Message();
      *m = msg;
      _eventLoop->asyncCall(0,
        boost::bind(&TcpTransportSocketPrivate::send_, this, boost::ref(*m), true));
    }
    return true;
  }

  bool TcpTransportSocketPrivate::send_(const qi::Message &msg, bool allocated)
  {
    if (!_connected)
    {
      qiLogError("qimessaging.TcpTransportSocket") << "socket is not connected.";
      return false;
    }

    qi::Message *m;
    if (allocated)
      m = const_cast<qi::Message*>(&msg);
    else
    {
      m = new qi::Message;
      *m = msg;
    }

    m->_p->complete();

    struct evbuffer *mess = evbuffer_new();
    // m might be deleted.
    qi::Buffer *b = new qi::Buffer(m->buffer());
    // Send header
    if (evbuffer_add_reference(mess,
                               m->_p->getHeader(),
                               sizeof(qi::MessagePrivate::MessageHeader),
                               qi::TcpTransportSocketPrivate::onMessageSent,
                               static_cast<void *>(m)) != 0)
    {
      qiLogError("qimessaging.TransportSocketLibevent") << "Add reference fail in header";
      evbuffer_free(mess);
      delete m;
      delete b;
      return false;
    }
    size_t sz = b->size();
    const std::vector<std::pair<uint32_t, Buffer> >& subs = b->subBuffers();
    size_t pos = 0;
    // Handle subbuffers
    for (unsigned i=0; i< subs.size(); ++i)
    {
      // Send parent buffer between pos and start of sub
      size_t end = subs[i].first;
      qiLogDebug("qimessaging.TransportSocketLibevent")
        << "serializing from " << pos <<" to " << end << " of " << sz;
      if (end != pos)
        if (evbuffer_add_reference(mess,
                                 (const char*)b->data() + pos,
                                 end - pos,
                                 0, 0) != 0)
        {
          qiLogError("qimessaging.TransportSocketLibevent") << "Add reference fail for block of size " << sz;
          evbuffer_free(mess);
          delete b;
          return false;
        }
      pos = subs[i].first;
      // Send subbuffer
      qiLogDebug("qimessaging.TransportSocketLibevent")
        << "serializing subbuffer of size " << subs[i].second.size();
      if (evbuffer_add_reference(mess,
                                 subs[i].second.data(),
                                 subs[i].second.size(),
                                 0, 0) != 0)
      {
        qiLogError("qimessaging.TransportSocketLibevent") << "Add reference fail for block of size " << sz;
        evbuffer_free(mess);
        delete b;
        return false;
      }
    }
    // Send last chunk of parent buffer between pos and end
    if (evbuffer_add_reference(mess,
                               (const char*)b->data() + pos,
                               b->size() - pos,
                               qi::TcpTransportSocketPrivate::onBufferSent,
                               static_cast<void *>(b)) != 0)
    {
      qiLogError("qimessaging.TransportSocketLibevent") << "Add reference fail for block of size " << sz;
      evbuffer_free(mess);
      delete b;
      return false;
    }

    _dispatcher.sent(msg);

    if (bufferevent_write_buffer(_bev, mess) != 0)
    {
      qiLogError("qimessaging.TransportSocketLibevent") << "Can't add buffer to the send queue";
      evbuffer_free(mess);
      //TODO: remove from dispatcher here
      return false;
    }
    evbuffer_free(mess);
    return true;
  }

}

