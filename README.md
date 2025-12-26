jsonbench
=========

Welcome to the `jsonbench` documentation.

## Description

The purpose of this tool is to compare the performance of different JSON C libraries and to show what to expect from a possible alternative JSON processor. For more details, take a look at the tool's source. Currently, mod_security2 and libmodsecurity3 use YAJL for JSON processing. Unfortunately, this library is deprecated and is no longer supported by its maintainer.

Note that mod_security2 processes JSON and creates keys and values. Keys are the names of the JSON keys and values are their values. If the JSON object is an array, the key will be the string `array`. For multidimensional arrays, the key is `array.array.....`.

## Requirements

To compile the code you need the any of these JSON C libraries (hopefully this will be expand):
* yajl
* RapidJSON

To install them, try these commands:

```bash
sudo apt install libyajl-dev libyajl2 rapidjson-dev
```

Note, that RapidJSON is a "header-only" parser, you need to install `rapidjson-dev`.

You also need `autoconf`, `automake`, `autotools-dev`, `make` and `gcc` or `clang` packages:

```bash
sudo apt install autoconf automake autotools-dev make gcc
```

## Build

Download and unpack the source, and run these commands

```bash
./autogen.sh
./configure
make
```
If you don't want to use any available JSON library, you can ignore it, eg:
```bash
./configure --wit-yajl=no
```

### Other `configure` flags:
For the complete list, please type `./configure --help`.

Relevant flags:
```
  --with-file-buffer-size=SIZE
                          Set buffer size (default: 524288 bytes (512kB))
  --with-json-string-size=SIZE
                          Set buffer size for JSON strings (key, values)
                          (default: 2048 bytes (2kB))
```
I didn't want to use dynamic strings and use `malloc()`/`calloc()`, therefore I used static length buffer in every cases. With these options, you can set the buffer's size.

The `--with-file-buffer-size` describes how big file you want to process. The default value is 512kB.

The `--with-json-string-size` describes how long can be a processed json key and value. The default value is 2kB. You can owervrite these values with these options. The values are in bytes.

## Run

To run the tool, type:
```bash
src/jsonbench -h
```

Or if you want to try a parser:
```bash
src/jsonbench -e YAJL
```

or

```bash
src/jsonbench -e RAPIDJSON
```

### Other command line flags

`src/jsonbench -h` gives a full list of available options. Here is a detailed review:

* `-e` sets the JSON engine (if there is any). Currently the tool supports YAJL and RAPIDJSON, so you can pass `-e YAJL` or `-e RAPIDJSON` (with capital letters).
* `-d` sets the maximum depth of the JSON strcture. The default value is 500. This option is the same as the [SecRequestBodyJsonDepthLimit](https://github.com/owasp-modsecurity/ModSecurity/wiki/Reference-Manual-(v2.x)#user-content-SecRequestBodyJsonDepthLimit) in ModSecurity. When the processor reached this limit with the processed depth, the tool terminates with error.
* `-a` sets the maximum size of arguments. The default value is 500. This option is the same as the [SecArgumentsLimit](https://github.com/owasp-modsecurity/ModSecurity/wiki/Reference-Manual-(v2.x)#secargumentslimit) in ModSecurity. When the processor reached this limit with the processed arguments, the tool terminates with error.
* `-s` keeps the tool in silence, and don't print out the processed data. This can be help to measure the performance more accurate.

### Example

```
src/jsonbench -e YAJL -a 10000 -s tests/512KB.json 

Time: 0.002567988 usec
```
This command processes the file `tests/512KB.json` and prints out the total time of JSON processing. Eg. this does not include the file processing.

```
src/jsonbench -e YAJL -a 10000 tests/test06.json 
array.array.id: 2489651045
array.array.type: CreateEvent
array.array.actor.id: 665991
array.array.actor.login: petroav
array.array.actor.gravatar_id: 
array.array.actor.url: https://api.github.com/users/petroav
array.array.actor.avatar_url: https://avatars.githubusercontent.com/u/665991?
array.array.repo.id: 28688495
array.array.repo.name: petroav/6.828
array.array.repo.url: https://api.github.com/repos/petroav/6.828
array.array.payload.ref: master
array.array.payload.ref_type: branch
array.array.payload.master_branch: master
...
array.array.public: true
array.array.created_at: 2015-01-01T15:00:01Z

Time: 0.000747715 usec
```
This command process the file `tests/test06.json` and prints out the processed keys and values. The syntax is the same as in case of mod_security2 JSON processing.

## TODO

Add more possible JSON processors.
