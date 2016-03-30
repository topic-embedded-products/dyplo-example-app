dyplo-example-app
=================

To build this project in OpenEmbedded, just use the dyplo-example-app.bb recipe in meta-dyplo.

To build and run the example application on a desktop machine:

- Download and build the Dyplo library (libdyplo on github)
````
  git clone git://github.com/topic-embedded-products/libdyplo.git
  cd libdyplo
  autoreconf --install
  mkdir build
  cd build
  ../configure --prefix $HOME
  make -j 4
  make install
````
- Add the local libraries to the PKG_CONFIG path
````  
  export PKG_CONFIG_PATH=${HOME}/lib/pkgconfig
````
- Download and build this example application:
````
  git clone git://github.com/topic-embedded-products/dyplo-example-app.git
  cd dyplo-example-app
  autoreconf --install
  mkdir build
  cd build
  ../configure --prefix $HOME
  make
````

