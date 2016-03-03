/*
 * Copyright (C) 2015 deipi.com LLC and contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "server_raft.h"

#include "raft.h"
#include "server.h"

#include <assert.h>


RaftServer::RaftServer(const std::shared_ptr<XapiandServer>& server_, ev::loop_ref *loop_, const std::shared_ptr<Raft> &raft_)
	: BaseServer(server_, loop_, raft_->sock),
	raft(raft_)
{
	// accept event actually started in BaseServer::BaseServer
	L_EV(this, "Start raft's server accept event (sock=%d)", raft->sock);

	L_OBJ(this, "CREATED RAFT SERVER! [%p]", this);
}


RaftServer::~RaftServer()
{
	L_OBJ(this, "DELETED RAFT SERVER! [%p]", this);
}


void
RaftServer::io_accept_cb(ev::io &watcher, int revents)
{
	L_EV_BEGIN(this, "RaftServer::io_accept_cb:BEGIN");
	if (EV_ERROR & revents) {
		L_EV(this, "ERROR: got invalid raft event (sock=%d): %s", raft->sock, strerror(errno));
		L_EV_END(this, "RaftServer::io_accept_cb:END");
		return;
	}

	assert(raft->sock == watcher.fd || raft->sock == -1);

	if (revents & EV_READ) {
		char buf[1024];
		struct sockaddr_in addr;
		socklen_t addrlen = sizeof(addr);

		ssize_t received = ::recvfrom(watcher.fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, &addrlen);

		if (received < 0) {
			if (!ignored_errorno(errno, true)) {
				L_ERR(this, "ERROR: read error (sock=%d): %s", raft->sock, strerror(errno));
				manager()->shutdown();
			}
		} else if (received == 0) {
			// If no messages are available to be received and the peer has performed an orderly shutdown.
			L_CONN(this, "Received EOF (sock=%d)!", raft->sock);
			manager()->shutdown();
		} else {
			L_UDP_WIRE(this, "Raft: (sock=%d) -->> '%s'", raft->sock, repr(buf, received).c_str());

			if (received < 4) {
				L_RAFT(this, "Badly formed message: Incomplete!");
				L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
				return;
			}

			uint16_t remote_protocol_version = *(uint16_t *)(buf + 1);
			if ((remote_protocol_version & 0xff) > XAPIAND_RAFT_PROTOCOL_MAJOR_VERSION) {
				L_RAFT(this, "Badly formed message: Protocol version mismatch %x vs %x!", remote_protocol_version & 0xff, XAPIAND_RAFT_PROTOCOL_MAJOR_VERSION);
				L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
				return;
			}

			const char *ptr = buf + 3;
			const char *end = buf + received;

			std::string remote_cluster_name;
			if (unserialise_string(remote_cluster_name, &ptr, end) == -1 || remote_cluster_name.empty()) {
				L_RAFT(this, "Badly formed message: No cluster name!");
				L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
				return;
			}

			Node remote_node;
			if (remote_node.unserialise(&ptr, end) == -1) {
				L_RAFT(this, "Badly formed message: No proper node!");
				L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
				return;
			}

			if (remote_cluster_name != manager()->cluster_name || local_node.region.load() != remote_node.region.load()) {
				L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
				return;
			}

			std::string vote;
			std::string str_remote_term, str_servers;
			uint64_t remote_term;

			char cmd = buf[0];
			switch (cmd) {
				case toUType(Raft::Message::REQUEST_VOTE):
					raft->register_activity();

					if (unserialise_string(str_remote_term, &ptr, end) == -1) {
						L_RAFT(this, "Badly formed message: No proper term!");
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}
					remote_term = std::stoull(str_remote_term);

					L_RAFT(this, "remote_term: %llu  local_term: %llu", remote_term, raft->term);

					if (remote_term > raft->term) {
						if (raft->state == Raft::State::LEADER && remote_node != local_node) {
							L_ERR(this, "ERROR: Node %s (with highest term) does not receive this node as a leader. Therefore, this node will reset!", remote_node.name.c_str());
							raft->reset();
						}

						raft->votedFor = lower_string(remote_node.name);
						raft->term = remote_term;

						L_RAFT(this, "It Vote for %s", raft->votedFor.c_str());
						raft->send_message(Raft::Message::RESPONSE_VOTE, remote_node.serialise() +
							serialise_string("1") + serialise_string(str_remote_term));
					} else {
						if (raft->state == Raft::State::LEADER && remote_node != local_node) {
							L_ERR(this, "ERROR: Remote node %s does not recognize this node (with highest term) as a leader. Therefore, remote node will reset!", remote_node.name.c_str());
							raft->send_message(Raft::Message::RESET, remote_node.serialise());
							L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
							return;
						}

						if (remote_term < raft->term) {
							L_RAFT(this, "Vote for %s", raft->votedFor.c_str());
							raft->send_message(Raft::Message::RESPONSE_VOTE, remote_node.serialise() +
								serialise_string("0") + serialise_string(std::to_string(raft->term)));
						} else if (raft->votedFor.empty()) {
							raft->votedFor = lower_string(remote_node.name);
							L_RAFT(this, "Vote for %s", raft->votedFor.c_str());
							raft->send_message(Raft::Message::RESPONSE_VOTE, remote_node.serialise() +
								serialise_string("1") + serialise_string(std::to_string(raft->term)));
						} else {
							L_RAFT(this, "Vote for %s", raft->votedFor.c_str());
							raft->send_message(Raft::Message::RESPONSE_VOTE, remote_node.serialise() +
								serialise_string("0") + serialise_string(std::to_string(raft->term)));
						}
					}
					break;

				case toUType(Raft::Message::RESPONSE_VOTE):
					raft->register_activity();

					if (remote_node == local_node && raft->state == Raft::State::CANDIDATE) {
						if (unserialise_string(vote, &ptr, end) == -1) {
							L_RAFT(this, "Badly formed message: No proper vote!");
							L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
							return;
						}

						if (vote == "1") {
							++raft->votes;
							L_RAFT(this, "Number of servers: %d;  Votos received: %d", raft->number_servers.load(), raft->votes);
							if (raft->votes > raft->number_servers / 2) {
								L_RAFT(this, "It becomes the leader for region: %d", local_node.region.load());
								raft->state = Raft::State::LEADER;
								raft->start_heartbeat();
							}
							L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
							return;
						}

						if (unserialise_string(str_remote_term, &ptr, end) == -1) {
							L_RAFT(this, "Badly formed message: No proper term!");
							L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
							return;
						}
						remote_term = std::stoull(str_remote_term);

						if (raft->term < remote_term) {
							raft->term = remote_term;
							raft->state = Raft::State::FOLLOWER;
							L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
							return;
						}
					}
					break;

				case toUType(Raft::Message::LEADER):
					raft->register_activity();

					if (raft->state == Raft::State::LEADER) {
						assert(remote_node == local_node);
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}

					if (unserialise_string(str_servers, &ptr, end) == -1) {
						L_RAFT(this, "Badly formed message: No proper number of servers!");
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}
					raft->number_servers.store(std::stoull(str_servers));

					if (unserialise_string(str_remote_term, &ptr, end) == -1) {
						L_RAFT(this, "Badly formed message: No proper term!");
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}
					raft->term = std::stoull(str_remote_term);

					raft->leader = lower_string(remote_node.name);
					raft->state = Raft::State::FOLLOWER;
					break;

				case toUType(Raft::Message::RESET):
					if (local_node == remote_node) {
						raft->reset();
					}
					break;

				case toUType(Raft::Message::REQUEST_DATA):
					if (raft->state == Raft::State::LEADER) {
						L_DEBUG(this, "Sending Data!");
						raft->send_message(Raft::Message::RESPONSE_DATA, local_node.serialise() +
							serialise_string(std::to_string(raft->number_servers)) +
							serialise_string(std::to_string(raft->term)));
					}
					break;

				case toUType(Raft::Message::RESPONSE_DATA):
					raft->register_activity();

					if (raft->state == Raft::State::LEADER) {
						assert(remote_node == local_node);
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}

					L_DEBUG(this, "Receiving Data!");
					if (unserialise_string(str_servers, &ptr, end) == -1) {
						L_RAFT(this, "Badly formed message: No proper number of servers!");
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}
					raft->number_servers.store(std::stoull(str_servers));

					if (unserialise_string(str_remote_term, &ptr, end) == -1) {
						L_RAFT(this, "Badly formed message: No proper term!");
						L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
						return;
					}
					raft->term = std::stoull(str_remote_term);

					raft->leader = lower_string(remote_node.name);
					break;

				case toUType(Raft::Message::HEARTBEAT_LEADER):
					raft->register_activity();
					if (raft->leader != lower_string(remote_node.name)) {
						L_RAFT(this, "Request the raft server's configuration!");
						raft->send_message(Raft::Message::REQUEST_DATA, local_node.serialise());
					}
					// L_RAFT(this, "Listening %s's heartbeat in timestamp: %f!", remote_node.name.c_str(), raft->last_activity);
					break;
			}
		}
	}

	L_EV_END(this, "RaftServer::io_accept_cb:END %lld", now);
}
