/*
**
** Copyright (C) 2012 Aldebaran Robotics
*/

#include <qi/application.hpp>
#include <qitype/genericobject.hpp>
#include <qitype/genericobjectbuilder.hpp>
#include <qitype/objectfactory.hpp>
#include <qimessaging/session.hpp>

int testMethod(const int& v)
{
  return v+1;
}

qi::ObjectPtr setup(const std::string&)
{
  qiLogDebug("testmodule") << "setup";
  qi::GenericObjectBuilder ob;
  ob.advertiseMethod("testMethod", testMethod);
  return ob.object();
}

QI_REGISTER_OBJECT_FACTORY("test", &setup);
