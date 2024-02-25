# PnP Server

This is a webserver/application server, built to demonstrate the [TinyServer](https://github.com/robertofig85/TinyServer) framework. It has a webserver frontend to handle basic HTTP static requests, and the hability to add apps to it, to handle dynamic content.

PnP stands for Plug-and-Play, a concept of simplicity and ease of use. Getting the server up and running is just a matter of running the executable, and adding apps to it is as simple as compiling a dynamic library (DLL/SO) and dropping it inside the root folder. The server automatically detects and loads the apps present on startup.

## How to use?

After building it (see build instructions below), the server is a single executable called `pnp-server`. You can run it from command line, with an optional argument to set the root directory, files folder, and port:

```
>: pnp-server [path-to-root-dir] [files-folder-name] [port]
```

If not specified, the default root dir is the `current working directory`, the default files folder name is `wwwroot`, and the default port is `8080`.

The root dir is where everything happens. The files folder must be inside it, which is the base for the static files (so, the resource for `www.my-website.com/index.html` will be located at `[root-dir]/[files-folder]/index.html`). All files served by the webserver must be inside it.

It is also where apps are located. Each app must have its own folder, and inside it the compiled app (as a dynamic library), as well as auxiliary files the app may need. The folder name and app filename must be the same. A schematics of a root dir is as follows:

```
root
  |--- [files-folder]
  |       |--- index.html
  |       |--- script.js
  |--- [app1]
  |       |--- app1.dll
  |--- [app2]
          |--- app2.dll
          |--- table.xml
```

All one needs to do is drop the folder in the root dir, the server will automatically add it to the list and have it up and running. The filename of the app determines the URL path to access it. So, if you have an file called `myapp.dll`, the path to access it is `www.my-website.com/myapp`. This will make the webserver enter the app, and it takes control from there.

## How to build the server

The build script is located in `/src`, and builds to a `/build` directory in the project root. It can be simply run from command line, provided the compiler symbols are loaded. Currently only supports MSVC on Windows, but a version for Clang is in the making, and in the future a version for Linux using GCC. It is configured to build both a release version and a debug version, although debug version comes commented out.

## How to build an app

To build an app to it, one must included the [pnp-server-app.h](src/pnp-server-app.h) header file, present in the `/src` dir. You can copy it to the app working area and include it from there. It must also contain the following two functions:

```c
void ModuleMain(http* Http, app_arena* Arena);

void AppInit(app_arena* Arena);
```

The app must be built as a dynamic library. When building the app, these two functions must have unmangled symbol name (use `extern "C"` when compiling in C++) and must be exported, as the symbols are loaded during runtime.

## How do apps work?

Every app has a persistent memory buffer just for it, stored in an `app_arena` object. It has a [.Ptr] to the top of the buffer, a [.WriteCur] for the amount written so far in it, and the total size of it in [.Size]. It is passed to the application via the `http` object in ModuleMain. Several threads may be accessing it at the same time, it is the app's job to handle concurrent writes. The app is also responsible for updating the WriteCur, and making sure it does not write past Size.

Every app must also implement at least one method, called `ModuleMain`. It is the app's entry point from the webserver. It takes a pointer to an `http` object, and a pointer to its `app_arena`. The `http` object contains details from the request, such as URI, number of headers, if it has a body or not, etc (more details can be found in `pnp-server-app.h`). It also has methods for accessing the request data, and writing back the response (the formatting is taken care of by the server). After the app finishes processing, it exits ModuleMain returning nothing.

The app may also implement a second method, called `AppInit`. This one is not required, but if implemented it is called once, at server startup. It takes a pointer to the `app_arena` object, so that data can be initialised and stored in it (such as database connections, file reads, etc.). The function does not need to use it, however, and can be used in any way the app sees fit.

To make sure that the app does not hog all processing power from the server, the server spawns a new thread when it enters the app, and all apps compete for the same cores, leaving the IO threads of the webserver free to run. This does mean that, as the number of connections grow the response time for requests that enter the apps grows as well, but that is the downside of running an app server on the same machine as the webserver. Regardless, this only starts becoming a problem after thousands of concurrent connections.

## Examples

The `/example` folder contains a working example of a website with two apps:

* [Prime](/example/src/prime.c):  it receives a number from the client and returns if it's prime, or the closest prime if not. It uses the AppInit() function to load a table of primes into the app_arena.
* [Modify](/example/src/modify.cpp): it receives an image uploaded by the client and returns it in sepia or greyscale.

The source of both apps is in `/example/src`, and must be built with the build script in it in order to run. After they are built, run the server pointing the root directory to `/example`, then start navigating from `localhost:[PORT]/index.html`. The apps are accessed from there.

## License

MIT open source license.