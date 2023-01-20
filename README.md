# mark-files

![mark-files help](https://github.com/strinque/mark-files/blob/master/docs/help.png)

The `mark-files` **Windows** program extracts files properties, such as *creation-time*/*modification-time*/*hash_sha256*, from a directory.  
It stores the output into a `json` file.  With the `--restore` command-line option, it can restore the creation-time/modification-time when the dates are different from the ones saved if the hash is identical.

Implemented in c++17 and use `vcpkg`/`cmake` for the build-system.  

It uses the `winpp` header-only library from: https://github.com/strinque/winpp.

## Features

![mark-files process](https://github.com/strinque/mark-files/blob/master/docs/process.gif)

- [x] use `nlohmann/json` header-only library for `json` parsing
- [x] use multi-threading to retrieve the hash of the files
- [x] restore ctime/mtime of files when the hash hasn't changed

## Usage

``` console
# extract files properties and store the output in a json file
mark-files.exe --path "c:\directory" \
               --output "database.json"
```

## Requirements

This project uses **vcpkg**, a free C/C++ package manager for acquiring and managing libraries to build all the required libraries.  
It also needs the installation of the **winpp**, a private *header-only library*, inside **vcpkg**.

### Install vcpkg

The install procedure can be found in: https://vcpkg.io/en/getting-started.html.  

Open PowerShell: 

``` console
cd c:\
git clone https://github.com/Microsoft/vcpkg.git
.\vcpkg\bootstrap-vcpkg.bat
vcpkg integrate install
```

Create a `VCPKG_ROOT` environment variable which points to this vcpkg directory: 

``` console
setx VCPKG_ROOT "c:\vcpkg"
```
This environment variable will be used by **Visual Studio** to locate the `vcpkg` directory.

Create a `x64-windows-static-md` tripplet file used to build the program in shared-mode for **Windows** libraries but static-mode for third-party libraries:

``` console
Set-Content "$env:VCPKG_ROOT\triplets\community\x64-windows-static-md.cmake" 'set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)'
```

### Install winpp ports-files

Copy the *vcpkg ports files* from **winpp** *header-only library* repository to the **vcpkg** directory.

``` console
mkdir $env:VCPKG_ROOT\ports\winpp
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/strinque/winpp/master/vcpkg/ports/winpp/portfile.cmake" -OutFile "$env:VCPKG_ROOT\ports\winpp\portfile.cmake"
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/strinque/winpp/master/vcpkg/ports/winpp/vcpkg.json" -OutFile "$env:VCPKG_ROOT\ports\winpp\vcpkg.json"
```

## Build

### Build using cmake

To build the program with `vcpkg` and `cmake`, follow these steps:

``` console
git clone https://github.com/strinque/mark-files
cd mark-files
mkdir build; cd build
cmake -DCMAKE_BUILD_TYPE="MinSizeRel" `
      -DVCPKG_TARGET_TRIPLET="x64-windows-static-md" `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
      ../
cmake --build .
```

The program executable should be compiled in: `mark-files\build\src\MinSizeRel\mark-files.exe`.

### Build with Visual Studio

**Microsoft Visual Studio** can automatically install required **vcpkg** libraries and build the program thanks to the pre-configured files: 

- `CMakeSettings.json`: debug and release settings
- `vcpkg.json`: libraries dependencies

The following steps needs to be executed in order to build/debug the program:

``` console
File => Open => Folder...
  Choose mark-files root directory
Solution Explorer => Switch between solutions and available views => CMake Targets View
Select x64-release or x64-debug
Select the src\mark-files.exe (not bin\mark-files.exe)
```

To add command-line arguments for debugging the program:

```
Solution Explorer => Project => (executable) => Debug and Launch Settings => src\program.exe
```

``` json
  "args": [
    "--path \"c:\\directory\"",
    "--output database.json"
  ]
```