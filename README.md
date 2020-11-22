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

## Event service configs

#### PullPoint

"IgnoreClientsTimeout" - boolean value specifies whether ingore or not a timeout value from the PullMessages request. Currently this value is ignored and supposed always be true, i.e. it is hardcoded in the code. Actual timeout value equals to the value specified in "Timeout".

"Timeout" - PullMessages timeout in seconds.

 
