import { Compiler } from 'webpack';
const path = require('path');
const fs = require('fs');
const zlib = require("zlib");

export = SPIFFSUpdate;
// Define the interface for the plugin options
interface SPIFFSUpdateOptions {
    sourceFiles?: string[];
    targetFiles?: string[];
    compress?: boolean;
}

class SPIFFSUpdate {
    private sourceFiles: string[];
    private targetFiles: string[];
    private compress: boolean;

    constructor(options: SPIFFSUpdateOptions) {
        this.sourceFiles = options.sourceFiles || []; // Array of proto_path directories
        this.targetFiles = options.targetFiles || []; // Array of proto source files or directories
        this.compress = options.compress || true; // Output directory
    }

    apply(compiler: Compiler) {
        compiler.hooks.afterEmit.tapAsync('GrpcToolsNodeProtocPlugin', (compilation, callback) => {
            try {

                fs.appendFileSync('./dist/index.html.gz',
                    zlib.gzipSync(fs.readFileSync('./dist/index.html'),
                        {
                            chunckSize: 65536,
                            level: zlib.constants.Z_BEST_COMPRESSION
                        })
                );
        

                //             if (options.mode !== "production") return;
                //             let buildRootPath = path.join(process.cwd(), '..', '..', '..');
                //             let wifiManagerPath = glob.sync(path.join(buildRootPath, 'components/**/wifi-manager*'))[0];
                //             let buildCRootPath = glob.sync(buildRootPath)[0];
                //             fs.appendFileSync('./dist/index.html.gz',
                //               zlib.gzipSync(fs.readFileSync('./dist/index.html'),
                //                 {
                //                   chunckSize: 65536,
                //                   level: zlib.constants.Z_BEST_COMPRESSION
                //                 }));

                //             var getDirectories = function (src, callback) {
                //               var searchPath = path.posix.join(src, '/**/*(*.gz|favicon-32x32.png)');
                //               console.log(`Post build: Getting file list from ${searchPath}`);
                //               glob(searchPath, callback);
                //             };
                //             var cleanUpPath = path.posix.join(buildCRootPath, '/build/*.S');
                //             console.log(`Post build: Cleaning up previous builds in ${cleanUpPath}`);
                //             glob(cleanUpPath, function (err, list) {
                //               if (err) {
                //                 console.error('Error', err);
                //               } else {
                //                 list.forEach(fileName => {
                //                   try {
                //                     console.log(`Post build: Purging old binary file ${fileName} from C project.`);
                //                     fs.unlinkSync(fileName)
                //                     //file removed
                //                   } catch (ferr) {
                //                     console.error(ferr)
                //                   }
                //                 });
                //               }
                //             },
                //               'afterEmit'
                //             );
                //             console.log('Generating C include files from webpack build output');
                //             getDirectories('./dist', function (err, list) {
                //               console.log(`Post build: found ${list.length} files. Relative path: ${wifiManagerPath}.`);
                //               if (err) {
                //                 console.log('Error', err);
                //               } else {

                //                 let exportDefHead =
                //                   `/***********************************
                // webpack_headers
                // ${arguments[1]}
                // ***********************************/
                // #pragma once
                // #include <inttypes.h>
                // extern const char * resource_lookups[];
                // extern const uint8_t * resource_map_start[];
                // extern const uint8_t * resource_map_end[];`;
                //                 let exportDef = '// Automatically generated. Do not edit manually!.\n' +
                //                   '#include <inttypes.h>\n';
                //                 let lookupDef = 'const char * resource_lookups[] = {\n';
                //                 let lookupMapStart = 'const uint8_t * resource_map_start[] = {\n';
                //                 let lookupMapEnd = 'const uint8_t * resource_map_end[] = {\n';
                //                 let cMake = '';

                //                 list.forEach(foundFile => {
                //                   let exportName = path.basename(foundFile).replace(/[\. \-]/gm, '_');
                //                   //take the full path of the file and make it relative to the build directory
                //                   let cmakeFileName = path.posix.relative(wifiManagerPath, glob.sync(path.resolve(foundFile))[0]);
                //                   let httpRelativePath = path.posix.join('/', path.posix.relative('dist', foundFile));
                //                   exportDef += `extern const uint8_t _${exportName}_start[] asm("_binary_${exportName}_start");\nextern const uint8_t _${exportName}_end[] asm("_binary_${exportName}_end");\n`;
                //                   lookupDef += `\t"${httpRelativePath}",\n`;
                //                   lookupMapStart += '\t_' + exportName + '_start,\n';
                //                   lookupMapEnd += '\t_' + exportName + '_end,\n';
                //                   cMake += `target_add_binary_data( __idf_wifi-manager ${cmakeFileName} BINARY)\n`;
                //                   console.log(`Post build: adding cmake file reference to ${cmakeFileName} from C project, with web path ${httpRelativePath}.`);
                //                 });

                //                 lookupDef += '""\n};\n';
                //                 lookupMapStart = lookupMapStart.substring(0, lookupMapStart.length - 2) + '\n};\n';
                //                 lookupMapEnd = lookupMapEnd.substring(0, lookupMapEnd.length - 2) + '\n};\n';
                //                 try {
                //                   fs.writeFileSync('webapp.cmake', cMake);
                //                   fs.writeFileSync('webpack.c', exportDef + lookupDef + lookupMapStart + lookupMapEnd);
                //                   fs.writeFileSync('webpack.h', exportDefHead);
                //                   //file written successfully
                //                 } catch (e) {
                //                   console.error(e);
                //                 }
                //               }
                //             });
                //             console.log('Post build completed.');

                //           })










            } catch (error) {
                console.error('Error setting up grpc-tools protoc', error);
            }
            callback();
        });
    }
}

