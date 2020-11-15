# The server execution

A configurations directory may be passed as command line argument.
As default configurations' directory used "./server_configs".
NOTE: it's expected that the last slash '/' is not added to the passed value.
Example: "main.exe ./configs".


# The server configurations

## Common configs

"authentication" - current authentication method is choosen via this variable. Any values from "authenticationMethods" can be used.
"authenticationMethods" - enums available values. Here is they desctiption: "none" - authentication is not required; "ws-security" - only WS-Security; "digest" - only digest

### Event service configs

#### PullPoint

"IgnoreClientsTimeout" - boolean value specifies whether ingore or not a timeout value from the PullMessages request. Currently not implemented. Now it's value always true and the timeout value equals to the value specified in "Timeout".

"Timeout" - PullMessages timeout in seconds.

 