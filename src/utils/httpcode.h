// ============= httpcode.h =============

#ifndef HTTPCODE_H
#define HTTPCODE_H

// HTTP Status Codes
// Based on RFC 7231 (HTTP/1.1 Semantics and Content)

// 1xx Informational
constexpr int HTTP_CONTINUE = 100;
constexpr int HTTP_SWITCHING_PROTOCOLS = 101;
constexpr int HTTP_PROCESSING = 102;

// 2xx Success
constexpr int HTTP_OK = 200;
constexpr int HTTP_CREATED = 201;
constexpr int HTTP_ACCEPTED = 202;
constexpr int HTTP_NON_AUTHORITATIVE_INFORMATION = 203;
constexpr int HTTP_NO_CONTENT = 204;
constexpr int HTTP_RESET_CONTENT = 205;
constexpr int HTTP_PARTIAL_CONTENT = 206;
constexpr int HTTP_MULTI_STATUS = 207;
constexpr int HTTP_ALREADY_REPORTED = 208;
constexpr int HTTP_IM_USED = 226;

// 3xx Redirection
constexpr int HTTP_MULTIPLE_CHOICES = 300;
constexpr int HTTP_MOVED_PERMANENTLY = 301;
constexpr int HTTP_FOUND = 302;
constexpr int HTTP_SEE_OTHER = 303;
constexpr int HTTP_NOT_MODIFIED = 304;
constexpr int HTTP_USE_PROXY = 305;
constexpr int HTTP_TEMPORARY_REDIRECT = 307;
constexpr int HTTP_PERMANENT_REDIRECT = 308;

// 4xx Client Errors
constexpr int HTTP_BAD_REQUEST = 400;
constexpr int HTTP_UNAUTHORIZED = 401;
constexpr int HTTP_PAYMENT_REQUIRED = 402;
constexpr int HTTP_FORBIDDEN = 403;
constexpr int HTTP_NOT_FOUND = 404;
constexpr int HTTP_METHOD_NOT_ALLOWED = 405;
constexpr int HTTP_NOT_ACCEPTABLE = 406;
constexpr int HTTP_PROXY_AUTHENTICATION_REQUIRED = 407;
constexpr int HTTP_REQUEST_TIMEOUT = 408;
constexpr int HTTP_CONFLICT = 409;
constexpr int HTTP_GONE = 410;
constexpr int HTTP_LENGTH_REQUIRED = 411;
constexpr int HTTP_PRECONDITION_FAILED = 412;
constexpr int HTTP_PAYLOAD_TOO_LARGE = 413;
constexpr int HTTP_URI_TOO_LONG = 414;
constexpr int HTTP_UNSUPPORTED_MEDIA_TYPE = 415;
constexpr int HTTP_RANGE_NOT_SATISFIABLE = 416;
constexpr int HTTP_EXPECTATION_FAILED = 417;
constexpr int HTTP_IM_A_TEAPOT = 418;
constexpr int HTTP_MISDIRECTED_REQUEST = 421;
constexpr int HTTP_UNPROCESSABLE_ENTITY = 422;
constexpr int HTTP_LOCKED = 423;
constexpr int HTTP_FAILED_DEPENDENCY = 424;
constexpr int HTTP_TOO_EARLY = 425;
constexpr int HTTP_UPGRADE_REQUIRED = 426;
constexpr int HTTP_PRECONDITION_REQUIRED = 428;
constexpr int HTTP_TOO_MANY_REQUESTS = 429;
constexpr int HTTP_REQUEST_HEADER_FIELDS_TOO_LARGE = 431;
constexpr int HTTP_UNAVAILABLE_FOR_LEGAL_REASONS = 451;

// 5xx Server Errors
constexpr int HTTP_INTERNAL_SERVER_ERROR = 500;
constexpr int HTTP_NOT_IMPLEMENTED = 501;
constexpr int HTTP_BAD_GATEWAY = 502;
constexpr int HTTP_SERVICE_UNAVAILABLE = 503;
constexpr int HTTP_GATEWAY_TIMEOUT = 504;
constexpr int HTTP_HTTP_VERSION_NOT_SUPPORTED = 505;
constexpr int HTTP_VARIANT_ALSO_NEGOTIATES = 506;
constexpr int HTTP_INSUFFICIENT_STORAGE = 507;
constexpr int HTTP_LOOP_DETECTED = 508;
constexpr int HTTP_NOT_EXTENDED = 510;
constexpr int HTTP_NETWORK_AUTHENTICATION_REQUIRED = 511;

#endif // HTTPCODE_H
