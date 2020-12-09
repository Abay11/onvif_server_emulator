# About the project

Simple Onvif Server is a simulator of an IP device (camera, NVR) compatible with standardized ONVIF interfaces, which can be used for tests purpoces.

# Server execution

A configuration's directory may be passed as command line argument.
As default configurations' directory used "./server_configs".
NOTE: it's expected that the last slash '/' is not added to the passed value.
Example: "main.exe ./configs".


# Server configurations

Each ONVIF service is implemented as a separate instance. Each service may have its own configuration file in JSON format. Please note, that some configs may be hardcoded in the code, or may be not fully implemented yet. It's always recommended to check, if a config parameter actually affect to some behaviour.

## Common configs

"authentication" - current authentication method is choosen via this variable. Any values from "authenticationMethods" can be used.
"authenticationMethods" - enums available values. Here is they desctiption: "none" - authentication is not required; "ws-security" - only WS-Security; "digest" - only digest
"loggingLevel" - allowed values: ERROR, WARN, INFO, DEBUG, TRACE. Values list from highegt to lowest priority, i.e. if used level is INFO, all logs will be showed, except DEBUG and TRACE. If value is WARN - only errors and warnings messages will be showed.
"portForwardingSimulation" - this section in config is used to setup the server to return in url's specified http and rtsp ports, i.e. in that way the server actually will listen one ports but return another ports

## Device service configs

#### GetServices

"Enabled" - boolean value. Can be used to do not include specific service in the response list. If this flag is absent, default value will be true. For example, it will be usefull to enabling/disabling Media2 service.

## Event service configs

#### PullPoint

"IgnoreClientsTimeout" - boolean value specifies whether ingore or not a timeout value from the PullMessages request. Currently this value is ignored and supposed always be true, i.e. it is hardcoded in the code. Actual timeout value equals to the value specified in "Timeout".

"Timeout" - PullMessages timeout in seconds.

 ## Discovery service configs

 #### Probe match properties

 "UseStaticResponse" - values: true/false. Points whether to response to the discovery Probe match with static message. Content will be read from `discovery_service_responses/probe_match.responses`. Currently is implemented only static variant.