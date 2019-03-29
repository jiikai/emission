/*! @file 	util_server.h
	@brief 	Enumerations and global variables for HTTP response status codes and mime types.
*/

#ifndef _util_server_h_
#define _util_server_h_

/*
** MACROS
*/

#define UTIL_NHTTP_RES_CODE 62
#define UTIL_NIANA_MIME_TYPE 36
#define UTIL_HTTP_RES_STATUS(enum_code) util_http_res_status_code_msgs[enum_code]
#define UTIL_IANA_MIME_TYPE(enum_code) util_iana_mime_type_templates[enum_code]
/*
** TYPES
*/

enum util_http_response_code {

	/* 1xx Informational */
	HTTP_100_CONTINUE,
	HTTP_101_SWITCHING_PROTOCOLS,
	HTTP_102_PROCESSING_WEBDAV,
	HTTP_103_EARLY_HINTS,

	/* 2xx Success */
	HTTP_200_OK,
	HTTP_201_CREATED,
	HTTP_202_ACCEPTED,
	HTTP_203_NONAUTHORATIVE_INFORMATION_1_1,
	HTTP_204_NO_CONTENT,
	HTTP_205_RESET_CONTENT,
	HTTP_206_PARTIAL_CONTENT,
	HTTP_207_MULTISTATUS_WEBDAV,
	HTTP_208_ALREADY_REPORTED_WEBDAV,
	HTTP_226_IM_USED_WEBDAV,

	/* 3xx Redirection */
	HTTP_300_MULTIPLE_CHOICES,
	HTTP_301_MOVED_PERMANENTLY,
	HTTP_302_FOUND,
	HTTP_303_SEE_OTHER_1_1,
	HTTP_304_NOT_MODIFIED,
	HTTP_305_USE_PROXY_1_1,
	HTTP_306_SWITCH_PROXY,
	HTTP_307_TEMPORARY_REDIRECT_1_1,
	HTTP_308_PERMANENT_REDIRECT,

	/* 4xx Client Error */
	HTTP_400_BAD_REQUEST,
	HTTP_401_UNAUTHORIZED,
	HTTP_402_PAYMENT_REQUIRED,
	HTTP_403_FORBIDDEN,
	HTTP_404_NOT_FOUND,
	HTTP_405_METHOD_NOT_ALLOWED,
	HTTP_406_NOT_ACCEPTABLE,
	HTTP_407_PROXY_AUTHENTICATION_REQUIRED,
	HTTP_408_REQUEST_TIMEOUT,
	HTTP_409_CONFLICT,
	HTTP_410_GONE,
	HTTP_411_LENGTH_REQUIRED,
	HTTP_412_PRECONDITION_FAILED,
	HTTP_413_PAYLOAD_TOO_LARGE,
	HTTP_414_URI_TOO_LONG,
	HTTP_415_UNSUPPORTED_MEDIA_TYPE,
	HTTP_416_RANGE_NOT_SATISFIABLE,
	HTTP_417_EXPECTATION_FAILED,
	HTTP_418_IM_A_TEAPOT,
	HTTP_421_MISDIRECTED_REQUEST,
	HTTP_422_UNPROCESSABLE_ENTITY_WEBDAV,
	HTTP_423_LOCKED_WEBDAV,
	HTTP_424_FAILED_DEPENDENCY_WEBDAV ,
	HTTP_426_UPGRADE_REQUIRED, /* Include header "Upgrade" */
	HTTP_428_PRECONDITION_REQUIRED,
	HTTP_429_TOO_MANY_REQUESTS,
	HTTP_431_REQUEST_HEADER_FIELDS_TOO_LARGE,
	HTTP_451_UNAVAILABLE_FOR_LEGAL_REASONS,

	/* 5xx Server Error */
	HTTP_500_INTERNAL_SERVER_ERROR,
	HTTP_501_NOT_IMPLEMENTED,
	HTTP_502_BAD_GATEWAY,
	HTTP_503_SERVICE_UNAVAILABLE,
	HTTP_504_GATEWAY_TIMEOUT,
	HTTP_505_HTTP_VERSION_NOT_SUPPORTED,
	HTTP_506_VARIANT_ALSO_NEGOTIATES,
	HTTP_507_INSUFFICIENT_STORAGE_WEBDAV,
	HTTP_508_LOOP_DETECTED_WEBDAV,
	HTTP_510_NOT_EXTENDED,
	HTTP_511_NETWORK_AUTHENTICATION_REQUIRED

};

enum util_iana_mime_type {

	/* application */
	ECMASCRIPT,
	GZIP,
	JAVASCRIPT,
	JSON,
	OGG,
	PDF,
	POSTSCRIPT,
	RTF,
	VND_MS_FONTOBJECT,
	X_7ZIP,
	X_TAR,
	ZIP,

	/* audio */
	MIDI,
	MP3,
	MP4A,
	OGA,
	X_FLAC,

	/* font */
	OTF,
	TTF,
	WOFF,
	WOFF2,

	/* image */
	GIF,
	JPEG,
	PNG,
	SVG,
	SVG_PLUS_XML_FONT,
	TIFF,

	/* text */
	CSS,
	CSV,
	HTML,
	MARKDOWN,
	TXT,
	XML,

	/* video */
	MP4V,
	MPEG,
	OGV

};

/*
** GLOBAL VARIABLES
*/

const char * const util_http_res_status_code_msgs[UTIL_NHTTP_RES_CODE] = {

	/* 1xx Informational */
	"100 Continue",
	"101 Switching Protocols",
	"102 Processing", /* WebDAV */
	"103 Early Hints",

	/* 2xx Success */
	"200 OK",
	"201 Created",
	"202 Accepted",
	"203 Non-Authorative Information", /* >= HTTP 1.1 */
	"204 No Content",
	"205 Reset Content",
	"206 Partial Content",
	"207 Multi-Status", /* WebDAV */
	"208 Already Reported", /* WebDAV */
	"226 IM Used", /* WebDAV */

	/* 3xx Redirection */
	"300 Multiple Choices",
	"301 Moved Permanently",
	"302 Found",
	"303 See Other",  /* >= HTTP 1.1 */
	"304 Not Modified",
	"305 Use Proxy",  /* >= HTTP 1.1 */
	"306 Switch Proxy",
	"307 Temporary Redirect",  /* >= HTTP 1.1 */
	"308 Permanent Redirect",

	/* 4xx Client Error */
	"400 Bad Request",
	"401 Unauthorized",
	"402 Payment Required",
	"403 Forbidden",
	"404 Not Found",
	"405 Method Not Allowed",
	"406 Not acceptable",
	"407 Proxy authentication_required",
	"408 Request timeout",
	"409 Conflict",
	"410 Gone",
	"411 Length Required",
	"412 Precondition Failed",
	"413 Payload Too Large",
	"414 Uri Too Long",
	"415 Unsupported Media Type",
	"416 Range Not Satisfiable",
	"417 Expectation fFiled",
	"418 I'm a teapot",
	"421 Misdirected Request",
	"422 Unprocessable Entity", /* WebDAV */
	"423 Locked", /* WebDAV */
	"424 Failed Dependency", /* WebDAV */
	"426 Upgrade Required",
	"428 Precondition Required",
	"429 Too Many Requests",
	"431 Request Header Fields Too Large",
	"451 Unavailable For Legal Reasons",

	/* 5xx Server Error */
	"500 Internal Server Error",
	"501 Not Implemented",
	"502 Bad Gateway",
	"503 Service Unavailable",
	"504 Gateway Timeout",
	"505 HTTP Version Not Supported",
	"506 Variant Also Negotiates",
	"507 Insufficient Storage", /* WebDAV */
	"508 Loop Detected", /* WebDAV */
	"510 Not Extended",
	"511 Network Authentication Required"

};

const char * const util_iana_mime_type_templates[UTIL_NIANA_MIME_TYPE] = {

	/* application */
	"application/ecmascript",
	"application/gzip",
	"application/javascript",
	"application/json",
	"application/ogg",
	"application/pdf",
	"application/postscript",
	"application/rtf",
	"application/vnd.ms-fontobject",
	"application/x-7z-compressed",
	"application/x-tar",
	"application/zip",

	/* audio */
	"audio/midi",
	"audio/mp3",
	"audio/mp4",
	"audio/ogg",
	"audio/x-flac",

	/* font */
	"font/otf",
	"font/ttf",
	"font/woff",
	"font/woff2",

	/* image */
	"image/gif",
	"image/jpeg",
	"image/png",
	"image/svg",
	"image/svg+xml",
	"image/tiff",

	/* text */
	"text/css",
	"text/csv",
	"text/html",
	"text/markdown",
	"text/txt",
	"text/xml",

	/* video */
	"video/mp4",
	"video/mpeg",
	"video/ogg"

};

#endif /* _util_server_h_ */
