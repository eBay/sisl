# Settings Framework

Any production grade systems level application will have multiple configurations to enable tuning for different set of 
use cases. This is more true with cloud based applications. One common way to achieve the configurations is to put a 
#define or constexpr instead of hard coding. But tuning these parameters means the entire application has to be 
recompiled and redeployed. It is very expensive and disruptive operation.

Alternate to that would be to put as some sort of json file and each application can read the json and put in some
sort of structure or read the value from json file. However, reading directly from json is resource intensive and doing
so on every operation is wasteful and performance impacting. Also changing these settings means application needs to
write quiet a bit of code to reload these settings in a thread safe, memory safe manner.

This framework provides easy way to group the configuration for an application, and provides a thread safe, memory safe,
highly efficient access of the configuration and also provides easy way to reload the settings. It supports both 
hot-swappable and non-hot-swappable configurations.

## How it works
It uses flatbuffers serialization to represent the hierarchical schema of configurations. The schema provides all
possible values with their default values. Once defined application needs to add the generated code into their library 
and can use the method this framework provides to access the settings with the similar cost as accessing local variable.

Here are the steps one needs to follow to use this feature

### Step 1: Define config schema in a .fbs file
Create a file called .fbs in your main config source folder, with sample something like this. 

```
native_include "utility/non_null_ptr.hpp"; // <--- Need to include this to support full default configs 
namespace testapp;  // <--- Namespace you want this generated code to be part of.

attribute "hotswap";    // <---- These 2 attributes are to be included to do hotswap and deprecated keywords
attribute "deprecated";

// <--- Define the inner config. Levels can be arbitrary level deep, but each level would result in one more pointer
// indirection everytime we access --->
table DBConfig {
    // <--- Name to use and followed by its datatype and an optional default value. Supported data types are:
    // "string", "uint32", "uint64", "int", "bool", "byte", "short", "ushort", "long", "ulong", "float", "double",
    // "vector", "union"
    // If these values are not overridden during startup or runtime (in case of hotswappable), then this value is used.
    databaseHost: string;         
    databasePort: uint32 = 27017;
    numThreads: uint32 = 8;
    maxConnections: uint32 = 10 (hotswap);    // Hotswappable meaning it can be changed runtime without restart
    maxSupportedSize: uint64 = 1099511627776;
}

// <--- Top level structure. The name of the top level structure is called "SettingsName" and the filename where it is
// stored is called "SchemaName" --->
table TestAppSettings {
    version: uint32;
    dbconfig: DBConfig; // <-- Define inner level config here. The levels can be arbitrary deep.
}

root_type TestAppSettings; // <---- root_type <SettingsName>
```

There are 2 names need to note down here
a) SchemaName: It is the name of the file followed by .fbs. Example if the file is stored as "homestore_config.fbs", the
SchemaName is "homestore_config"
b) SettingsName: The name of the settings that will be used to define this. In the example it is "TestAppSettings"

### Step 2: Include the following lines in your CMakeLists
The .fbs need to convert to your source code using the following steps:

```
include(${CONAN_SISL_ROOT}/cmake/settings_gen.cmake)
settings_gen_cpp(${FLATBUFFERS_FLATC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generated/ <target_to_build> <path to schema file (.fbs)>)

# Example:
# settings_gen_cpp(${FLATBUFFERS_FLATC_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/generated/ test_settings tests/test_app_schema.fbs)
```

### Step 3: Initialize in your code to include these files
In your main include code or separate code, add the following lines outside your namespace definition

```c++
#include <settings/settings.hpp>
#include "generated/homeblks_config_generated.h"

// <--- Format is
// SETTINGS_INIT(<namespace>::<SettingsName>, <SchemaName>)
SETTINGS_INIT(testapp::TestAppSettings, testapp_config);
```

### Step 4: Access these config variable using the following ways 
```c++
std::cout << "Database port is " << SETTINGS_VALUE(testapp_config, dbconfig->databasePort) << "\n";
```
or

```c++
SETTINGS(testapp_config, s, {
    std::cout << "Database port is " << s.dbconfig.databasePort << "\n";
    std::cout << "Database port is " << s.dbconfig.numThreads << "\n";
});
```

While the first one is quick way to access one variable, the second method ensures that if you are accessing multiple
parameters and you wanted to be atomic (no override should happen between 2 access of the variables)

Since having SETTINGS_VALUE(schemaName, ...) is typically repetitive to the application, one can define convenient
macros to something like
```c++
#define MY_SETTINGS(...) SETTINGS(testapp_config, __VA_ARGS__)
#define MY_SETTINGS_VALUE(...) SETTINGS_VALUE(testapp_config, __VA_ARGS__)

// and access them as
std::cout << "Database port is " << MY_SETTINGS_VALUE(dbconfig->databasePort) << "\n";
```

### Step 5: If you want to override
If one wanted to override the configuration with new settings, one can put the overridden config in a json file and 
then call
```c++
SETTINGS_FACTORY(testapp_config).reload_file(json_filename);
```
or directly generate a json string and call
```c++
SETTINGS_FACTORY(testapp_config).reload_json(json_string);
```

These methods return a boolean, indicating the overriden configuration needs a restart of application or not. If it 
needs a restart, its caller responsibility to restart the app when it is convenient and until that time only settings
are not changed.

To get a reference json with existing parameters one can call
```c++
SETTINGS_FACTORY(testapp_config).save(json_filename);
```
This will write the existing settings into json file
