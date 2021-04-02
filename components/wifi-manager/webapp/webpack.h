/***********************************
webpack_headers
Hash: 0b6890f4337e767921f7
Version: webpack 4.46.0
Time: 273269ms
Built at: 2021-04-03 1:28:56
                                Asset       Size  Chunks                                Chunk Names
          ./js/index.0b6890.bundle.js    231 KiB       0  [emitted] [immutable]         index
       ./js/index.0b6890.bundle.js.br   31.5 KiB          [emitted]                     
       ./js/index.0b6890.bundle.js.gz   41.1 KiB          [emitted]                     
   ./js/node-modules.0b6890.bundle.js    266 KiB       1  [emitted] [immutable]  [big]  node-modules
./js/node-modules.0b6890.bundle.js.br   76.3 KiB          [emitted]                     
./js/node-modules.0b6890.bundle.js.gz   88.7 KiB          [emitted]                     
        ./js/runtime.0b6890.bundle.js   1.46 KiB       2  [emitted] [immutable]         runtime
     ./js/runtime.0b6890.bundle.js.br  644 bytes          [emitted]                     
     ./js/runtime.0b6890.bundle.js.gz  722 bytes          [emitted]                     
                    favicon-32x32.png  634 bytes          [emitted]                     
                           index.html   19.5 KiB          [emitted]                     
                        index.html.br   4.48 KiB          [emitted]                     
                        index.html.gz   5.46 KiB          [emitted]                     
                           sprite.svg    4.4 KiB          [emitted]                     
                        sprite.svg.br  912 bytes          [emitted]                     
Entrypoint index [big] = ./js/runtime.0b6890.bundle.js ./js/node-modules.0b6890.bundle.js ./js/index.0b6890.bundle.js
 [6] ./node_modules/bootstrap/dist/js/bootstrap-exposed.js 437 bytes {1} [built]
[11] ./src/sass/main.scss 1.55 KiB {0} [built]
[16] ./node_modules/remixicon/icons/Device/signal-wifi-fill.svg 323 bytes {1} [built]
[17] ./node_modules/remixicon/icons/Device/signal-wifi-3-fill.svg 327 bytes {1} [built]
[18] ./node_modules/remixicon/icons/Device/signal-wifi-2-fill.svg 327 bytes {1} [built]
[19] ./node_modules/remixicon/icons/Device/signal-wifi-1-fill.svg 327 bytes {1} [built]
[20] ./node_modules/remixicon/icons/Device/signal-wifi-line.svg 323 bytes {1} [built]
[21] ./node_modules/remixicon/icons/Device/battery-line.svg 315 bytes {1} [built]
[22] ./node_modules/remixicon/icons/Device/battery-low-line.svg 323 bytes {1} [built]
[23] ./node_modules/remixicon/icons/Device/battery-fill.svg 315 bytes {1} [built]
[24] ./node_modules/remixicon/icons/Media/headphone-fill.svg 318 bytes {1} [built]
[25] ./node_modules/remixicon/icons/Device/device-recover-fill.svg 329 bytes {1} [built]
[26] ./node_modules/remixicon/icons/Device/bluetooth-fill.svg 319 bytes {1} [built]
[27] ./node_modules/remixicon/icons/Device/bluetooth-connect-fill.svg 335 bytes {1} [built]
[37] ./src/index.ts + 1 modules 53.3 KiB {0} [built]
     | ./src/index.ts 1.36 KiB [built]
     | ./src/js/custom.js 51.8 KiB [built]
    + 23 hidden modules

WARNING in asset size limit: The following asset(s) exceed the recommended size limit (244 KiB).
This can impact web performance.
Assets: 
  ./js/node-modules.0b6890.bundle.js (266 KiB)

WARNING in entrypoint size limit: The following entrypoint(s) combined asset size exceeds the recommended limit (244 KiB). This can impact web performance.
Entrypoints:
  index (499 KiB)
      ./js/runtime.0b6890.bundle.js
      ./js/node-modules.0b6890.bundle.js
      ./js/index.0b6890.bundle.js


WARNING in webpack performance recommendations: 
You can limit the size of your bundles by using import() or require.ensure to lazy load some parts of your application.
For more info visit https://webpack.js.org/guides/code-splitting/
Child html-webpack-plugin for "index.html":
         Asset     Size  Chunks  Chunk Names
    index.html  556 KiB       0  
    Entrypoint undefined = index.html
    [0] ./node_modules/html-webpack-plugin/lib/loader.js!./src/index.ejs 20.3 KiB {0} [built]
    [1] ./node_modules/lodash/lodash.js 531 KiB {0} [built]
    [2] (webpack)/buildin/global.js 472 bytes {0} [built]
    [3] (webpack)/buildin/module.js 497 bytes {0} [built]
***********************************/
#pragma once
#include <inttypes.h>
extern const char * resource_lookups[];
extern const uint8_t * resource_map_start[];
extern const uint8_t * resource_map_end[];
