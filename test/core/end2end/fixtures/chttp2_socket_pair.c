/*
 *
 * Copyright 2014, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "test/core/end2end/end2end_tests.h"
#include "src/core/channel/client_channel.h"
#include "src/core/channel/connected_channel.h"
#include "src/core/channel/http_filter.h"
#include "src/core/channel/http_server_filter.h"
#include "src/core/iomgr/endpoint_pair.h"
#include "src/core/iomgr/iomgr.h"
#include "src/core/surface/channel.h"
#include "src/core/surface/client.h"
#include "src/core/surface/server.h"
#include "src/core/transport/chttp2_transport.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>
#include <grpc/support/thd.h>
#include <grpc/support/useful.h>
#include "test/core/util/port.h"
#include "test/core/util/test_config.h"

/* chttp2 transport that is immediately available (used for testing
   connected_channel without a client_channel */

static grpc_transport_setup_result server_setup_transport(
    void *ts, grpc_transport *transport, grpc_mdctx *mdctx) {
  grpc_end2end_test_fixture *f = ts;
  static grpc_channel_filter const *extra_filters[] = {&grpc_http_server_filter,
                                                       &grpc_http_filter};
  return grpc_server_setup_transport(f->server, transport, extra_filters,
                                     GPR_ARRAY_SIZE(extra_filters), mdctx);
}

typedef struct {
  grpc_end2end_test_fixture *f;
  grpc_channel_args *client_args;
} sp_client_setup;

static grpc_transport_setup_result client_setup_transport(
    void *ts, grpc_transport *transport, grpc_mdctx *mdctx) {
  sp_client_setup *cs = ts;

  const grpc_channel_filter *filters[] = {&grpc_client_surface_filter,
                                          &grpc_connected_channel_filter};
  size_t nfilters = sizeof(filters) / sizeof(*filters);
  grpc_channel *channel = grpc_channel_create_from_filters(
      filters, nfilters, cs->client_args, mdctx, 1);

  cs->f->client = channel;

  return grpc_connected_channel_bind_transport(
      grpc_channel_get_channel_stack(channel), transport);
}

static grpc_end2end_test_fixture chttp2_create_fixture_socketpair(
    grpc_channel_args *client_args, grpc_channel_args *server_args) {
  grpc_endpoint_pair *sfd = gpr_malloc(sizeof(grpc_endpoint_pair));

  grpc_end2end_test_fixture f;
  f.fixture_data = sfd;
  f.client_cq = grpc_completion_queue_create();
  f.server_cq = grpc_completion_queue_create();
  f.server = grpc_server_create_from_filters(f.server_cq, NULL, 0, server_args);
  f.client = NULL;

  *sfd = grpc_iomgr_create_endpoint_pair(65536);

  return f;
}

static void chttp2_init_client_socketpair(grpc_end2end_test_fixture *f,
                                          grpc_channel_args *client_args) {
  grpc_endpoint_pair *sfd = f->fixture_data;
  sp_client_setup cs;
  cs.client_args = client_args;
  cs.f = f;
  grpc_create_chttp2_transport(client_setup_transport, &cs, client_args,
                               sfd->client, NULL, 0, grpc_mdctx_create(), 1);
  GPR_ASSERT(f->client);
}

static void chttp2_init_server_socketpair(grpc_end2end_test_fixture *f,
                                          grpc_channel_args *server_args) {
  grpc_endpoint_pair *sfd = f->fixture_data;
  grpc_create_chttp2_transport(server_setup_transport, f, server_args,
                               sfd->server, NULL, 0, grpc_mdctx_create(), 0);
}

static void chttp2_tear_down_socketpair(grpc_end2end_test_fixture *f) {
  gpr_free(f->fixture_data);
}

/* All test configurations */
static grpc_end2end_test_config configs[] = {
    {"chttp2/socketpair", 0, chttp2_create_fixture_socketpair,
     chttp2_init_client_socketpair, chttp2_init_server_socketpair,
     chttp2_tear_down_socketpair},
};

int main(int argc, char **argv) {
  size_t i;

  grpc_test_init(argc, argv);
  grpc_init();

  for (i = 0; i < sizeof(configs) / sizeof(*configs); i++) {
    grpc_end2end_tests(configs[i]);
  }

  grpc_shutdown();

  return 0;
}