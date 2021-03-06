/**
 *  Copyright (c) 2014-present, Facebook, Inc.
 *  All rights reserved.
 *
 *  This source code is licensed under both the Apache 2.0 license (found in the
 *  LICENSE file in the root directory of this source tree) and the GPLv2 (found
 *  in the COPYING file in the root directory of this source tree).
 *  You may select, at your option, one of the above-listed licenses.
 */

#pragma once

#include <osquery/dispatcher.h>
#include <osquery/extensions.h>

#ifdef WIN32
#pragma warning(push, 3)

/*
 * MSVC complains that ExtensionManagerHandler inherits the call() function from
 * ExtensionHandler via dominance. This is because ExtensionManagerHandler
 * implements ExtensionManagerIf and ExtensionHandler who both implement
 * ExtensionIf. ExtensionIf declares a virtual call() function that
 * ExtensionHandler defines. This _shouldn't_ cause any issues.
 */
#pragma warning(disable : 4250)
#endif

#ifdef FBTHRIFT
#include <thrift/lib/cpp/async/TAsyncSocket.h>
#include <thrift/lib/cpp2/async/HeaderClientChannel.h>
#include <thrift/lib/cpp2/protocol/BinaryProtocol.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#else
#include <thrift/concurrency/ThreadManager.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/server/TThreadedServer.h>
#include <thrift/transport/TBufferTransports.h>
#endif

#ifdef WIN32
#include <thrift/transport/TPipe.h>
#include <thrift/transport/TPipeServer.h>
#elif !defined(FBTHRIFT)
#include <thrift/transport/TServerSocket.h>
#include <thrift/transport/TSocket.h>
#endif

// Include intermediate Thrift-generated interface definitions.
#include "Extension.h"
#include "ExtensionManager.h"

#ifdef FBTHRIFT
#define API_PING sync_ping
#define API_CALL sync_call
#define API_QUERY sync_query
#define API_COLUMNS sync_getQueryColumns
#define API_REGISTER sync_registerExtension
#define API_OPTIONS sync_options
#define API_EXTENSIONS sync_extensions
#define API_SHUTDOWN sync_shutdown
#else
#define API_PING ping
#define API_CALL call
#define API_QUERY query
#define API_COLUMNS getQueryColumns
#define API_REGISTER registerExtension
#define API_OPTIONS options
#define API_EXTENSIONS extensions
#define API_SHUTDOWN shutdown
#endif

namespace osquery {

using namespace apache::thrift;
using namespace apache::thrift::protocol;
using namespace apache::thrift::transport;
using namespace apache::thrift::server;
using namespace apache::thrift::concurrency;

#ifdef WIN32
typedef TPipe TPlatformSocket;
typedef TPipeServer TPlatformServerSocket;
typedef std::shared_ptr<TPipe> TPlatformSocketRef;
#elif !defined(FBTHRIFT)
typedef TSocket TPlatformSocket;
typedef TServerSocket TPlatformServerSocket;
typedef std::shared_ptr<TSocket> TPlatformSocketRef;
#endif

typedef std::shared_ptr<TTransport> TTransportRef;
typedef std::shared_ptr<TProtocol> TProtocolRef;
typedef std::shared_ptr<TServerTransport> TServerTransportRef;

#ifdef FBTHRIFT
typedef std::shared_ptr<AsyncProcessorFactory> TProcessorRef;
using TThreadedServerRef = std::shared_ptr<ThriftServer>;
using _Client = extensions::cpp2::ExtensionAsyncClient;
using _ManagerClient = extensions::cpp2::ExtensionManagerAsyncClient;
#else
typedef std::shared_ptr<TProcessor> TProcessorRef;
typedef std::shared_ptr<TTransportFactory> TTransportFactoryRef;
typedef std::shared_ptr<TProtocolFactory> TProtocolFactoryRef;
typedef std::shared_ptr<ThreadManager> TThreadManagerRef;
using TThreadedServerRef = std::shared_ptr<TThreadedServer>;
using _Client = extensions::ExtensionClient;
using _ManagerClient = extensions::ExtensionManagerClient;
#endif

namespace extensions {

#ifdef FBTHRIFT
using namespace cpp2;
using _ExtensionIf = ExtensionSvIf;
using _ExtensionManagerIf = ExtensionManagerSvIf;
using _str_param = const std::unique_ptr<std::string>;
using _plugin_param = const std::unique_ptr<ExtensionPluginRequest>;
using _info_param = const std::unique_ptr<InternalExtensionInfo>;
using _registry_param = const std::unique_ptr<ExtensionRegistry>;
#else
using _ExtensionIf = ExtensionIf;
using _ExtensionManagerIf = ExtensionManagerIf;
using _str_param = const std::string&;
using _plugin_param = const ExtensionPluginRequest&;
using _info_param = const InternalExtensionInfo&;
using _registry_param = const ExtensionRegistry&;
#endif

/**
 * @brief The Thrift API server used by an osquery Extension process.
 *
 * An extension will load and start a thread to serve the ExtensionHandler
 * Thrift runloop. This handler is the implementation of the thrift IDL spec.
 * It implements all the Extension API handlers.
 *
 */
class ExtensionHandler : virtual public _ExtensionIf {
 public:
  ExtensionHandler() : uuid_(0) {}
  explicit ExtensionHandler(RouteUUID uuid) : uuid_(uuid) {}

  /// Ping an Extension for status and metrics.
  void ping(ExtensionStatus& _return) override;

  /**
   * @brief The Thrift API used by Registry::call for an extension route.
   *
   * @param _return The return response (combo Status and PluginResponse).
   * @param registry The name of the Extension registry.
   * @param item The Extension plugin name.
   * @param request The plugin request.
   */
  void call(ExtensionResponse& _return,
            _str_param registry,
            _str_param item,
            _plugin_param request) override;

  /// Request an extension to shutdown.
  virtual void shutdown() override;

 protected:
  /// Transient UUID assigned to the extension after registering.
  std::atomic<RouteUUID> uuid_;
};

/**
 * @brief The Thrift API server used by an osquery process.
 *
 * An extension will load and start a thread to serve the
 * ExtensionManagerHandler. This listens for extensions and allows them to
 * register their Registry route information. Calls to the registry may then
 * match a route exposed by an extension.
 * This handler is the implementation of the thrift IDL spec.
 * It implements all the ExtensionManager API handlers.
 *
 */
class ExtensionManagerHandler : virtual public _ExtensionManagerIf,
                                public ExtensionHandler {
 public:
  ExtensionManagerHandler();

  /// Return a list of Route UUIDs and extension metadata.
  void extensions(InternalExtensionList& _return) override;

  /**
   * @brief Return a map of osquery options (Flags, bootstrap CLI flags).
   *
   * osquery options are set via command line flags or overridden by a config
   * options dictionary. There are some CLI-only flags that should never
   * be overridden. If a bootstrap flag is changed there is undefined behavior
   * since bootstrap candidates are settings needed before a configuration
   * plugin is setUp.
   *
   * Extensions may broadcast config or logger plugins that need a snapshot
   * of the current options. The best example is the `config_plugin` bootstrap
   * flag.
   */
  void options(InternalOptionList& _return) override;

  /**
   * @brief Request a Route UUID and advertise a set of Registry routes.
   *
   * When an Extension starts it must call registerExtension using a well known
   * ExtensionManager UNIX domain socket path. The ExtensionManager will check
   * the broadcasted routes for duplicates as well as enforce SDK version
   * compatibility checks. On success the Extension is returned a Route UUID and
   * begins to serve the ExtensionHandler Thrift API.
   *
   * @param _return The output Status and optional assigned RouteUUID.
   * @param info The osquery Thrift-internal Extension metadata container.
   * @param registry The Extension's Registry::getBroadcast information.
   */
  void registerExtension(ExtensionStatus& _return,
                         _info_param info,
                         _registry_param registry) override;

  /**
   * @brief Request an Extension removal and removal of Registry routes.
   *
   * When an Extension process is graceful killed it should deregister.
   * Other privileged tools may choose to deregister an Extension by
   * the transient Extension's Route UUID, obtained using
   * ExtensionManagerHandler::extensions.
   *
   * @param _return The output Status.
   * @param uuid The assigned Route UUID to deregister.
   */
  void deregisterExtension(ExtensionStatus& _return,
                           const ExtensionRouteUUID uuid) override;

  /**
   * @brief Execute an SQL statement in osquery core.
   *
   * Extensions do not have access to the internal SQLite implementation.
   * For complex queries (beyond select all from a table) the statement must
   * be passed into SQLite.
   *
   * @param _return The output Status and QueryData (as response).
   * @param sql The sql statement.
   */
  void query(ExtensionResponse& _return, _str_param sql) override;

  /**
   * @brief Get SQL column information for SQL statements in osquery core.
   *
   * Extensions do not have access to the internal SQLite implementation.
   * For complex queries (beyond metadata for a table) the statement must
   * be passed into SQLite.
   *
   * @param _return The output Status and TableColumns (as response).
   * @param sql The sql statement.
   */
  void getQueryColumns(ExtensionResponse& _return, _str_param sql) override;

 protected:
  /// A shutdown request does not apply to ExtensionManagers.
  void shutdown() override {}

 private:
  /// Check if an extension exists by the name it registered.
  bool exists(const std::string& name);

  /// Introspect into the registry, checking if any extension routes have been
  /// removed.
  void refresh();

  /// Maintain a map of extension UUID to metadata for tracking deregistration.
  InternalExtensionList extensions_;

  /// Mutex for extensions accessors.
  Mutex extensions_mutex_;
};

typedef std::shared_ptr<ExtensionHandler> ExtensionHandlerRef;
typedef std::shared_ptr<ExtensionManagerHandler> ExtensionManagerHandlerRef;
}

/// A Dispatcher service thread that watches an ExtensionManagerHandler.
class ExtensionWatcher : public InternalRunnable {
 public:
  virtual ~ExtensionWatcher() = default;
  ExtensionWatcher(const std::string& path, size_t interval, bool fatal);

 public:
  /// The Dispatcher thread entry point.
  void start() override;

  /// Perform health checks.
  virtual void watch();

 protected:
  /// Exit the extension process with a fatal if the ExtensionManager dies.
  void exitFatal(int return_code = 1);

 protected:
  /// The UNIX domain socket path for the ExtensionManager.
  std::string path_;

  /// The internal in milliseconds to ping the ExtensionManager.
  size_t interval_;

  /// If the ExtensionManager socket is closed, should the extension exit.
  bool fatal_;
};

class ExtensionManagerWatcher : public ExtensionWatcher {
 public:
  ExtensionManagerWatcher(const std::string& path, size_t interval)
      : ExtensionWatcher(path, interval, false) {}

  /// The Dispatcher thread entry point.
  void start() override;

  /// Start a specialized health check for an ExtensionManager.
  void watch() override;

 private:
  /// Allow extensions to fail for several intervals.
  std::map<RouteUUID, size_t> failures_;
};

class ExtensionRunnerCore : public InternalRunnable {
 public:
  virtual ~ExtensionRunnerCore();
  explicit ExtensionRunnerCore(const std::string& path)
      : InternalRunnable("ExtensionRunnerCore"),
        path_(path),
        server_(nullptr) {}

 public:
  /// Given a handler transport and protocol start a thrift threaded server.
  void startServer(TProcessorRef processor);

  // The Dispatcher thread service stop point.
  void stop() override;

 protected:
  /// The UNIX domain socket used for requests from the ExtensionManager.
  std::string path_;

  /// Raw socket (optional)
  int raw_socket_{0};

  /// Transport instance, will be interrupted if the thread is removed.
  TServerTransportRef transport_{nullptr};

  /// Server instance, will be stopped if thread service is removed.
  TThreadedServerRef server_{nullptr};

  /// Protect the service start and stop, this mutex protects server creation.
  Mutex service_start_;

  /// Record a dispatcher's request to stop the service.
  bool service_stopping_{false};
};

/**
 * @brief A Dispatcher service thread that starts ExtensionHandler.
 *
 * This runner will start a Thrift Extension server, call serve, and wait
 * until the extension exists or the ExtensionManager (core) terminates or
 * deregisters the extension.
 *
 */
class ExtensionRunner : public ExtensionRunnerCore {
 public:
  ExtensionRunner(const std::string& manager_path, RouteUUID uuid);

 public:
  void start() override;

  /// Access the UUID provided by the ExtensionManager.
  RouteUUID getUUID() const;

 private:
  /// The unique and transient Extension UUID assigned by the ExtensionManager.
  RouteUUID uuid_;
};

/**
 * @brief A Dispatcher service thread that starts ExtensionManagerHandler.
 *
 * This runner will start a Thrift ExtensionManager server, call serve, and wait
 * until for extensions to register, or thrift API calls.
 *
 */
class ExtensionManagerRunner : public ExtensionRunnerCore {
 public:
  virtual ~ExtensionManagerRunner();
  explicit ExtensionManagerRunner(const std::string& manager_path)
      : ExtensionRunnerCore(manager_path) {}

 public:
  void start() override;
};

/// Internal accessor for extension clients.
class EXInternal : private boost::noncopyable {
 public:
  explicit EXInternal(const std::string& path);

  // Set the receive and send timeout.
  void setTimeouts(size_t timeout);

  virtual ~EXInternal();

 protected:
  std::string path_;
  int raw_socket_{0};

#ifdef FBTHRIFT
  folly::EventBase base_;
#else
  TPlatformSocketRef socket_;
  TTransportRef transport_;
  TProtocolRef protocol_;
#endif
};

/// Internal accessor for a client to an extension (from an extension manager).
class EXClient : public EXInternal {
 public:
  /**
   * @brief Create a client to a client extension.
   *
   * @note The default timeout to wait for buffered (whole-content) responses
   * is 5 minutes.
   * @param path This is the socket path for the client communication.
   * @param timeout [optional] time in milliseconds to wait for input.
   */
  explicit EXClient(const std::string& path, size_t timeout = 5000 * 60);

  const std::shared_ptr<_Client>& get() const;

 private:
  std::shared_ptr<_Client> client_;
};

/// Internal accessor for a client to an extension manager (from an extension).
class EXManagerClient : public EXInternal {
 public:
  /**
   * @brief Create a client to a manager extension.
   *
   * @param path This is the socket path for the manager communication.
   * @param timeout [optional] time in milliseconds to wait for input.
   */
  explicit EXManagerClient(const std::string& manager_path,
                           size_t timeout = 5000 * 60);

  const std::shared_ptr<_ManagerClient>& get() const;

 private:
  std::shared_ptr<_ManagerClient> client_;
};
}

#ifdef WIN32
#pragma warning(pop)
#endif
