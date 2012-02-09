/*
** network_client.hpp
** Login : <ctaf42@donnetout>
** Started on  Thu Feb  2 11:59:48 2012 Cedric GESTES
** $Id$
**
** Author(s):
**  - Cedric GESTES <gestes@aldebaran-robotics.com>
**
** Copyright (C) 2012 Cedric GESTES
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef   	NETWORK_CLIENT_HPP_
# define   	NETWORK_CLIENT_HPP_

#include <qimessaging/transport/transport_socket.hpp>
#include <qimessaging/datastream.hpp>

#include <vector>
#include <string>

namespace qi {

  class NetworkThread;

  class MachineInfo {
  public:
    std::string uuid;
  };

  class EndpointInfo {
  public:
    int port;
    std::string  ip;
    std::string  type;

    bool operator==(const EndpointInfo &b) const
    {
      if (port == b.port &&
          ip == b.ip &&
          type == b.type)
        return true;

      return false;
    }

  };

  static DataStream& operator<<(DataStream &d, const EndpointInfo &e)
  {
    d << e.ip;
    d << e.port;
    d << e.type;

    return d;
  }

  static DataStream& operator>>(DataStream &d, EndpointInfo &e)
  {
    d >> e.ip;
    d >> e.port;
    d >> e.type;

    return d;
  }

  class ServiceInfo {
  public:
    std::string name;
    std::vector<qi::EndpointInfo> endpoint;
  };


  class Session : public qi::TransportSocketDelegate {
  public:
    Session();
    virtual ~Session();

    void onConnected(const qi::Message &msg);
    void onWrite(const qi::Message &msg);
    void onRead(const qi::Message &msg);


    void connect(const std::string &masterAddress);
    bool disconnect();

    bool waitForConnected(int msecs = 30000);
    bool waitForDisconnected(int msecs = 30000);

    void registerMachine(const qi::MachineInfo& m);
    void unregisterMachine(const qi::MachineInfo& m);

    void registerEndpoint(const qi::EndpointInfo& e);
    void unregisterEndpoint(const qi::EndpointInfo& e);

    std::vector<std::string> machines();
    std::vector<std::string> services();

    qi::TransportSocket* service(const std::string &name,
                                 const std::string &type = "tcp");

    void setName(const std::string &name) { _name = name; }
    std::string name()                    { return _name; }

    void setDestination(const std::string &destination) { _destination = destination; }
    std::string destination()                           { return _destination; }

    bool isInitialized() const;

    qi::TransportSocket *tc;
  protected:
    std::string _destination;
    std::string _name;

    bool                 _isInitialized;
    qi::NetworkThread   *_nthd;
  };
}


#endif	    /* !NETWORK_CLIENT_PP_ */
