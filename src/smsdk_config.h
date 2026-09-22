#pragma once

#define SMEXT_CONF_NAME "REST"
#define SMEXT_CONF_DESCRIPTION "Asynchronous HTTP client with streamed transfers"
#define SMEXT_CONF_VERSION SM_REST_VERSION
#define SMEXT_CONF_AUTHOR "BuSheezy"
#define SMEXT_CONF_URL "https://github.com/BadServersNet/sm-rest"
#define SMEXT_CONF_LOGTAG "REST"
#define SMEXT_CONF_LICENSE "GPL"
#define SMEXT_CONF_DATESTRING __DATE__

#define SMEXT_LINK(name) SDKExtension *g_pExtensionIface = name;

#define SMEXT_ENABLE_FORWARDSYS
#define SMEXT_ENABLE_HANDLESYS
#define SMEXT_ENABLE_LIBSYS
#define SMEXT_ENABLE_PLUGINSYS
