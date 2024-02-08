#include "tes_http_server.hpp"

// TODO: Use *user_data to pass around this data to remove these
aws_http_stream *req_han;
aws_allocator *_allocator = aws_default_allocator();
aws_http_headers *all_headers;
aws_http_message *response;
aws_http_server *_server;
aws_event_loop_group *e_group;
aws_server_bootstrap *server_bootstrap;

const auto LOG = ggapi::Logger::of("TesHttpServerPlugin");

const char *get_tes_credentials() {
    // TODO: Request parameter should contain the authZ token set in the header
    // Fetch credentials from TES Plugin
    auto tes_lpc_request{ggapi::Struct::create()};
    tes_lpc_request.put("test", "some-unique-token");
    auto tes_lpc_response =
        ggapi::Task::sendToTopic(ggapi::Symbol{"aws.greengrass.requestTES"}, tes_lpc_request);
    auto tes_credentials = tes_lpc_response.get<std::string>({"Response"});
    return tes_credentials.c_str();
}

extern "C" int onRequestDone(struct aws_http_stream *stream, void *user_data) {
    const char *tes_credentials = get_tes_credentials();
    response = aws_http_message_new_response(_allocator);
    aws_http_message_set_response_status(response, 200);
    struct aws_byte_cursor body_src = aws_byte_cursor_from_c_str(tes_credentials);

    struct aws_input_stream *response_body =
        aws_input_stream_new_from_cursor(aws_default_allocator(), &body_src);
    aws_http_message_set_body_stream(response, response_body);

    std::string contentLength = std::to_string(body_src.len);
    struct aws_http_header headers[] = {
        {
            .name = aws_byte_cursor_from_c_str("Content-Type"),
            .value = aws_byte_cursor_from_c_str("application/json"),
        },
        {
            .name = aws_byte_cursor_from_c_str("Content-Length"),
            .value = aws_byte_cursor_from_c_str(contentLength.c_str()),
        },
    };
    aws_http_message_add_header_array(response, headers, AWS_ARRAY_SIZE(headers));
    if(aws_http_stream_send_response(req_han, response) != AWS_OP_SUCCESS) {
        LOG.atError().log("Failed to send credentials to the component with id: some-unique-id");
        return AWS_OP_ERR;
    }
    LOG.atDebug().log("Credentials are sent to the component with id: some-unique-id");
    return AWS_OP_SUCCESS;
}

extern "C" int onRequestHeadersDone(
    struct aws_http_stream *stream, enum aws_http_header_block header_block, void *user_data) {

    (void) header_block;
    if(stream->request_method != AWS_HTTP_METHOD_GET) {
        LOG.atError().log("Only GET requests are supported");
        return AWS_OP_ERR;
    }
    struct aws_byte_cursor request_uri;
    if(aws_http_stream_get_incoming_request_uri(stream, &request_uri) != AWS_OP_SUCCESS) {
        LOG.atError().log("Errored while fetching the request path URI.");
        return AWS_OP_ERR;
    }

    const char *supported_uri = "/2016-11-01/credentialprovider/";
    if(!aws_byte_cursor_eq_c_str(&request_uri, supported_uri)) {
        LOG.atError().log("Only /2016-11-01/credentialprovider/ uri is supported");
        return AWS_OP_ERR;
    }
    struct aws_byte_cursor authz_header_value;
    const char *tes_header = "Authorization";
    struct aws_byte_cursor tes_header_cursor = aws_byte_cursor_from_c_str(tes_header);
    if(aws_http_headers_get(all_headers, tes_header_cursor, &authz_header_value)
       != AWS_OP_SUCCESS) {
        LOG.atError().log("Authorization header is needed to process the request");
        return AWS_OP_ERR;
    }
    return AWS_OP_SUCCESS;
}

extern "C" int onIncomingRequestHeaders(
    struct aws_http_stream *stream,
    enum aws_http_header_block header_block,
    const struct aws_http_header *header_array,
    size_t num_headers,
    void *user_data) {
    (void) header_block;
    (void) user_data;
    (void) stream;
    const struct aws_http_header *in_header = header_array;
    for(size_t i = 0; i < num_headers; ++i) {
        aws_http_headers_add(all_headers, in_header->name, in_header->value);
    }
    return AWS_OP_SUCCESS;
}

extern "C" void onRequestComplete(struct aws_http_stream *stream, int error_code, void *user_data) {
    (void) stream;
    // TODO: Destroy response message
    aws_http_message_destroy(response);
}

extern "C" struct aws_http_stream *onIncomingRequest(
    struct aws_http_connection *connection, void *user_data) {
    all_headers = aws_http_headers_new(_allocator);

    struct aws_http_request_handler_options options = AWS_HTTP_REQUEST_HANDLER_OPTIONS_INIT;
    options.user_data = user_data;
    options.server_connection = connection;
    options.on_request_headers = onIncomingRequestHeaders;
    options.on_request_header_block_done = onRequestHeadersDone;
    options.on_complete = onRequestComplete;
    options.on_request_done = onRequestDone;
    req_han = aws_http_stream_new_server_request_handler(&options);
    return req_han;
}

extern "C" void onConnectionShutdown(
    aws_http_connection *connection, int error_code, void *connection_user_data) {
    // TODO: Clear the request handler if applicable
}

extern "C" void onIncomingConnection(
    struct aws_http_server *server,
    struct aws_http_connection *connection,
    int error_code,
    void *user_data) {
    if(error_code) {
        LOG.atWarn().log("Connection is not setup properly");
        // TODO: Close/clean up the connection?
        return;
    }
    struct aws_http_server_connection_options options = AWS_HTTP_SERVER_CONNECTION_OPTIONS_INIT;
    options.connection_user_data = user_data;
    options.on_incoming_request = onIncomingRequest;
    options.on_shutdown = onConnectionShutdown;
    int err = aws_http_connection_configure_server(connection, &options);
    if(err) {
        LOG.atWarn().log("Service is not configured properly with connection callback");
        // TODO: Close/clean up the connection?
        return;
    }
}

void TesHttpServer::start_server() {
    aws_http_library_init(_allocator);

    // Configure server options
    aws_http_server_options _serverOptions = AWS_HTTP_SERVER_OPTIONS_INIT;
    e_group = aws_event_loop_group_new_default(_allocator, 1, NULL);

    server_bootstrap = aws_server_bootstrap_new(_allocator, e_group);
    // TODO: Revisit this to check if there a way to get the randomly assigned port number. For now,
    // use 8080.

    aws_socket_endpoint _socketEndpoint{"127.0.0.1", 8080};
    aws_socket_options _socketOptions{
        .type = AWS_SOCKET_STREAM,
        .connect_timeout_ms = 3000,
        .keep_alive_timeout_sec = 10,
        .keepalive = true};
    _serverOptions.endpoint = &_socketEndpoint;
    _serverOptions.socket_options = &_socketOptions;
    _serverOptions.allocator = _allocator;
    _serverOptions.bootstrap = server_bootstrap;
    _serverOptions.on_incoming_connection = onIncomingConnection;

    _server = aws_http_server_new(&_serverOptions);

    if(_server != nullptr) {
        LOG.atInfo().log("Started TES HTTP server on port");
    }
}
void TesHttpServer::stop_server() {
    LOG.atInfo().log("Shutting down the TES HTTP server.");
    if(_server) {
        aws_http_server_release(_server);
        aws_server_bootstrap_release(server_bootstrap);
        aws_event_loop_group_release(e_group);
    }
    aws_http_library_clean_up();
}
