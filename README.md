# DecimaLoader

This is a basic plugin loader for Horizon Forbidden West PC.

## Usage

1. Download the latest release from the [releases page](https://github.com/Fexty12573/DecimaLoader/releases).
2. Extract the contents of the archive to the game's root directory.

Any plugins placed in the `plugins` directory will be loaded when the game starts.

## Building
Using Visual Studio 2022:

1. Clone the repository
2. Install [vcpkg](https://github.com/microsoft/vcpkg?tab=readme-ov-file#quick-start-windows)
3. Open the solution file in Visual Studio
4. Build the solution

## Creating Plugins
- Copy the `Loader.h` file from the `DecimaLoader` project into your plugin project.
- Add the following code to your plugin project:

```cpp
#include "Loader.h"

PLUGIN_API void plugin_initialize(PluginInitializeOptions* options) {
    // Your initialization code here
}
```

The `PluginInitializeOptions` struct contains a few fields you can use to subscribe to some events.

## Credits
- [ShadelessFox](https://github.com/ShadelessFox): The RTTI structures I used in the StorageExpander plugin are mostly from [here](https://github.com/ShadelessFox/decima-native).
