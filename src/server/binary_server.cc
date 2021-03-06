/*
 * Copyright (C) 2015-2018 Dubalu LLC. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "binary_server.h"

#ifdef XAPIAND_CLUSTERING

#include <errno.h>                          // for errno
#include <sysexits.h>                       // for EX_SOFTWARE

#include "binary.h"                         // for Binary
#include "binary_client.h"                  // for BinaryClient
#include "cassert.h"                        // for ASSERT
#include "endpoint.h"                       // for Endpoints
#include "error.hh"                         // for error:name, error::description
#include "fs.hh"                            // for exists
#include "ignore_unused.h"                  // for ignore_unused
#include "manager.h"                        // for XapiandManager
#include "readable_revents.hh"              // for readable_revents
#include "repr.hh"                          // for repr
#include "tcp.h"                            // for TCP::socket


// #undef L_DEBUG
// #define L_DEBUG L_GREY
// #undef L_CALL
// #define L_CALL L_STACKED_DIM_GREY
// #undef L_EV
// #define L_EV L_MEDIUM_PURPLE


BinaryServer::BinaryServer(const std::shared_ptr<Binary>& binary_, ev::loop_ref* ev_loop_, unsigned int ev_flags_, const char* hostname, unsigned int serv, int tries)
	: MetaBaseServer<BinaryServer>(binary_, ev_loop_, ev_flags_, "Binary", TCP_TCP_NODELAY | TCP_SO_REUSEPORT),
	  binary(*binary_),
	  trigger_replication_async(*ev_loop)
{
	bind(hostname, serv, tries);

	trigger_replication_async.set<BinaryServer, &BinaryServer::trigger_replication_async_cb>(this);
	trigger_replication_async.start();
	L_EV("Start binary's async trigger replication signal event");
}


BinaryServer::~BinaryServer() noexcept
{
	try {
		Worker::deinit();
	} catch (...) {
		L_EXC("Unhandled exception in destructor");
	}
}


void
BinaryServer::start_impl()
{
	L_CALL("BinaryServer::start_impl()");

	Worker::start_impl();

	io.start(sock == -1 ? binary.sock : sock, ev::READ);
	L_EV("Start binary's server accept event {sock:%d}", sock == -1 ? binary.sock : sock);
}


void
BinaryServer::trigger_replication()
{
	L_CALL("BinaryServer::trigger_replication()");

	trigger_replication_async.send();
}


void
BinaryServer::trigger_replication_async_cb(ev::async&, int revents)
{
	L_CALL("BinaryServer::trigger_replication_async_cb(<watcher>, 0x%x (%s))", revents, readable_revents(revents));

	L_EV_BEGIN("BinaryServer::trigger_replication_async_cb:BEGIN");
	L_EV_END("BinaryServer::trigger_replication_async_cb:END");

	ignore_unused(revents);

	TriggerReplicationArgs args;
	while (binary.trigger_replication_args.try_dequeue(args)) {
		trigger_replication(args);
	}
}


int
BinaryServer::accept()
{
	L_CALL("BinaryServer::accept()");

	if (sock != -1) {
		return TCP::accept();
	}
	return binary.accept();
}


void
BinaryServer::io_accept_cb(ev::io& watcher, int revents)
{
	L_CALL("BinaryServer::io_accept_cb(<watcher>, 0x%x (%s)) {sock:%d}", revents, readable_revents(revents), watcher.fd);

	L_EV_BEGIN("BinaryServer::io_accept_cb:BEGIN");
	L_EV_END("BinaryServer::io_accept_cb:END");

	ignore_unused(watcher);
	ASSERT(sock == -1 || sock == watcher.fd);

	L_DEBUG_HOOK("BinaryServer::io_accept_cb", "BinaryServer::io_accept_cb(<watcher>, 0x%x (%s)) {sock:%d}", revents, readable_revents(revents), watcher.fd);

	if ((EV_ERROR & revents) != 0) {
		L_EV("ERROR: got invalid binary event {sock:%d}: %s (%d): %s", watcher.fd, error::name(errno), errno, error::description(errno));
		return;
	}

	int client_sock = accept();
	if (client_sock != -1) {
		auto client = Worker::make_shared<BinaryClient>(share_this<BinaryServer>(), ev_loop, ev_flags, client_sock, active_timeout, idle_timeout);

		if (!client->init_remote()) {
			client->detach();
			return;
		}

		client->start();
	}
}


void
BinaryServer::trigger_replication(const TriggerReplicationArgs& args)
{
	if (args.src_endpoint.is_local()) {
		ASSERT(!args.cluster_database);
		return;
	}

	bool replicated = false;

	if (args.src_endpoint.path == "./") {
		// Cluster database is always updated
		replicated = true;
	}

	if (!replicated && exists(args.src_endpoint.path + "iamglass")) {
		// If database is already there, its also always updated
		replicated = true;
	}

	if (!replicated) {
		// Otherwise, check if the local node resolves as replicator
		auto local_node = Node::local_node();
		auto nodes = XapiandManager::resolve_index_nodes(args.src_endpoint.path);
		for (const auto& node : nodes) {
			if (Node::is_superset(local_node, node)) {
				replicated = true;
				break;
			}
		}
	}

	if (!replicated) {
		ASSERT(!args.cluster_database);
		return;
	}

	auto& node = args.src_endpoint.node;
	int port = (node.binary_port == XAPIAND_BINARY_SERVERPORT) ? XAPIAND_BINARY_PROXY : node.binary_port;
	auto& host = node.host();
	int client_sock = TCP::connect(host.c_str(), std::to_string(port).c_str());
	if (client_sock == -1) {
		if (args.cluster_database) {
			L_CRIT("Cannot replicate cluster database");
			sig_exit(-EX_SOFTWARE);
		}
		return;
	}
	L_CONN("Connected to %s! (in socket %d)", repr(src_endpoints.to_string()), client_sock);

	auto client = Worker::make_shared<BinaryClient>(share_this<BinaryServer>(), ev_loop, ev_flags, client_sock, active_timeout, idle_timeout, args.cluster_database);

	if (!client->init_replication(args.src_endpoint, args.dst_endpoint)) {
		client->detach();
		return;
	}

	client->start();
	L_DEBUG("Database %s being synchronized from %s%s" + DEBUG_COL + "...", repr(args.src_endpoint.to_string()), args.src_endpoint.node.col().ansi(), args.src_endpoint.node.name());
}


std::string
BinaryServer::__repr__() const
{
	return string::format("<BinaryServer {cnt:%ld, sock:%d}%s%s%s>",
		use_count(),
		sock == -1 ? binary.sock : sock,
		is_runner() ? " (runner)" : " (worker)",
		is_running_loop() ? " (running loop)" : " (stopped loop)",
		is_detaching() ? " (deteaching)" : "");
}

#endif /* XAPIAND_CLUSTERING */
