#include "tes_http_server.hpp"

const auto LOG = ggapi::Logger::of("TesHttpServerPlugin");
const auto requestTesCredentialsTopic = "aws.greengrass.requestTES";
const auto contentTypeHeader = "Content-Type";
const auto jsonTypeHeader = "application/json";
const auto contentLengthHeader = "Content-Length";
const auto tesAuthzHeader = "Authorization";
const auto credentialProviderUri = "/2016-11-01/credentialprovider/";

static struct ServerParams {
    aws_allocator *allocator{};
    aws_http_server *server{};
    aws_event_loop_group *e_group{};
    aws_server_bootstrap *server_bootstrap{};
} serverParams;

struct RequestHandlerParams {
    aws_http_headers *request_headers;
    aws_http_stream *request_handler;
    aws_http_message *response;
};

ggapi::Struct getTesCredentialsStruct() {
    // TODO: Request parameter should contain the authZ token set in the header
    // Fetch credentials from TES Plugin
    auto tes_lpc_request{ggapi::Struct::create()};
    tes_lpc_request.put("test", "some-unique-token");
    auto tes_lpc_response =
        ggapi::Task::sendToTopic(ggapi::Symbol{requestTesCredentialsTopic}, tes_lpc_request);
    return tes_lpc_response;
}

extern "C" {
static int onRequestDone(struct aws_http_stream *stream, void *user_data) {
    (void) stream;
    auto *requestParams = static_cast<RequestHandlerParams *>(user_data);
    const ggapi::Struct tes_credentials_struct = getTesCredentialsStruct();
    requestParams->response = aws_http_message_new_response(serverParams.allocator);
    if(tes_credentials_struct.hasKey("Response")) {

        auto tes_credentials = tes_credentials_struct.get<std::string>({"Response"});
        struct aws_byte_cursor body_src = aws_byte_cursor_from_c_str(tes_credentials.c_str());

        struct aws_input_stream *response_body =
            aws_input_stream_new_from_cursor(serverParams.allocator, &body_src);

        std::string contentLength = std::to_string(body_src.len);
        struct aws_http_header headers[] = {
            {
                .name = aws_byte_cursor_from_c_str(contentTypeHeader),
                .value = aws_byte_cursor_from_c_str(jsonTypeHeader),
            },
            {
                .name = aws_byte_cursor_from_c_str(contentLengthHeader),
                .value = aws_byte_cursor_from_c_str(contentLength.c_str()),
            },
        };

        // Build response message
        aws_http_message_set_body_stream(requestParams->response, response_body);
        aws_http_message_set_response_status(requestParams->response, AWS_HTTP_STATUS_CODE_200_OK);
        aws_http_message_add_header_array(
            requestParams->response, headers, AWS_ARRAY_SIZE(headers));
    } else {
        LOG.atError().log("Could not retrieve credentials from TES");

        // Build response message
        aws_http_message_set_response_status(
            requestParams->response, AWS_HTTP_STATUS_CODE_500_INTERNAL_SERVER_ERROR);
    }

    // Send response
    if(aws_http_stream_send_response(requestParams->request_handler, requestParams->response)
       != AWS_OP_SUCCESS) {
        LOG.atError().log("Failed to send response to the client");
        return AWS_OP_ERR;
    }
    LOG.atDebug().log("Response sent to the client");
    return AWS_OP_SUCCESS;
}

static int onRequestHeadersDone(
    struct aws_http_stream *stream, enum aws_http_header_block header_block, void *user_data) {

    auto *requestParams = static_cast<RequestHandlerParams *>(user_data);
    (void) header_block;
    if(stream->request_method != AWS_HTTP_METHOD_GET) {
        LOG.atError().log("Only GET requests are supported");
        return AWS_OP_ERR;
    }
    struct aws_byte_cursor request_uri {};
    if(aws_http_stream_get_incoming_request_uri(stream, &request_uri) != AWS_OP_SUCCESS) {
        LOG.atError().log("Errored while fetching the request path URI.");
        return AWS_OP_ERR;
    }

    if(!aws_byte_cursor_eq_c_str(&request_uri, credentialProviderUri)) {
        LOG.atError().log("Only /2016-11-01/credentialprovider/ uri is supported");
        return AWS_OP_ERR;
    }
    struct aws_byte_cursor authz_header_value {};
    struct aws_byte_cursor tes_header_cursor = aws_byte_cursor_from_c_str(tesAuthzHeader);
    if(aws_http_headers_get(requestParams->request_headers, tes_header_cursor, &authz_header_value)
       != AWS_OP_SUCCESS) {
        LOG.atError().log("Authorization header is needed to process the request");
        return AWS_OP_ERR;
    }
    return AWS_OP_SUCCESS;
}
static int onIncomingRequestHeaders(
    struct aws_http_stream *stream,
    enum aws_http_header_block header_block,
    const struct aws_http_header *header_array,
    size_t num_headers,
    void *user_data) {
    (void) header_block;
    (void) stream;
    auto *requestParams = static_cast<RequestHandlerParams *>(user_data);
    const struct aws_http_header *in_header = header_array;
    for(size_t i = 0; i < num_headers; ++i) {
        aws_http_headers_add(requestParams->request_headers, in_header->name, in_header->value);
    }
    return AWS_OP_SUCCESS;
}
static void onRequestComplete(struct aws_http_stream *stream, int error_code, void *user_data) {
    (void) stream;
    if(error_code) {
        LOG.atError().log("An error occurred while handling the request");
    }
    auto *requestParams = static_cast<RequestHandlerParams *>(user_data);
    aws_http_message_destroy(requestParams->response);
}
static struct aws_http_stream *onIncomingRequest(
    struct aws_http_connection *connection, void *user_data) {
    auto *requestParams = static_cast<RequestHandlerParams *>(user_data);
    requestParams->request_headers = aws_http_headers_new(serverParams.allocator);

    struct aws_http_request_handler_options options = AWS_HTTP_REQUEST_HANDLER_OPTIONS_INIT;
    options.user_data = requestParams;
    options.server_connection = connection;
    options.on_request_headers = onIncomingRequestHeaders;
    options.on_request_header_block_done = onRequestHeadersDone;
    options.on_complete = onRequestComplete;
    options.on_request_done = onRequestDone;
    requestParams->request_handler = aws_http_stream_new_server_request_handler(&options);
    return requestParams->request_handler;
}

static void onConnectionShutdown(
    aws_http_connection *connection, int error_code, void *connection_user_data) {
    (void) error_code;
    (void) connection_user_data;
    aws_http_connection_release(connection);
}

static void onIncomingConnection(
    struct aws_http_server *server,
    struct aws_http_connection *connection,
    int error_code,
    void *user_data) {

    if(error_code) {
        LOG.atWarn().log("Connection is not setup properly");
        return;
    }

    auto *requestParams = static_cast<RequestHandlerParams *>(user_data);
    struct aws_http_server_connection_options options = AWS_HTTP_SERVER_CONNECTION_OPTIONS_INIT;
    options.connection_user_data = requestParams;
    options.on_incoming_request = onIncomingRequest;
    options.on_shutdown = onConnectionShutdown;
    int err = aws_http_connection_configure_server(connection, &options);
    if(err) {
        LOG.atWarn().log("Service is not configured properly with connection callback");
        return;
    }
}

void TesHttpServer::startServer() {
    serverParams.allocator = aws_default_allocator();
    aws_http_library_init(serverParams.allocator);

    // Configure server options
    aws_http_server_options _serverOptions = AWS_HTTP_SERVER_OPTIONS_INIT;
    serverParams.e_group = aws_event_loop_group_new_default(serverParams.allocator, 1, NULL);

    serverParams.server_bootstrap =
        aws_server_bootstrap_new(serverParams.allocator, serverParams.e_group);
    // TODO: Revisit this to check if there a way to get the randomly assigned port number. For now,
    // use 8080.

    aws_socket_endpoint _socketEndpoint{"127.0.0.1", 8090};
    aws_socket_options _socketOptions{
        .type = AWS_SOCKET_STREAM,
        .connect_timeout_ms = 3000,
        .keep_alive_timeout_sec = 10,
        .keepalive = true};
    struct RequestHandlerParams requestParams {};
    void *pRequestHandlerParams = &requestParams;

    _serverOptions.endpoint = &_socketEndpoint;
    _serverOptions.socket_options = &_socketOptions;
    _serverOptions.allocator = serverParams.allocator;
    _serverOptions.bootstrap = serverParams.server_bootstrap;
    _serverOptions.on_incoming_connection = onIncomingConnection;
    _serverOptions.server_user_data = pRequestHandlerParams;
    serverParams.server = aws_http_server_new(&_serverOptions);

    if(serverParams.server != nullptr) {
        LOG.atInfo().log("Started TES HTTP server on port");
    } else {
        LOG.atError().log("Could not start the HTTP server");
    }
}
}

void TesHttpServer::stopServer() {
    LOG.atInfo().log("Shutting down the TES HTTP server.");
    if(serverParams.server) {
        aws_http_server_release(serverParams.server);
        aws_server_bootstrap_release(serverParams.server_bootstrap);
        aws_event_loop_group_release(serverParams.e_group);
    }
    aws_http_library_clean_up();
}
