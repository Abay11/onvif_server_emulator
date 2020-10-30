# The server execution

A configurations directory may be passed as command line argument.
As default configurations' directory used "./server_configs".
NOTE: it's expected that the last slash '/' is not added to the passed value.
Example: "main.exe ./configs".


# The server configurations

## Common configs

"authentication" - current authentication method is choosen via this variable. Any values from "authenticationMethods" can be used.
"authenticationMethods" - enums available values. Here is they desctiption: "none" - authentication is not required; "ws-security" - only WS-Security; "digest" - only digest

 