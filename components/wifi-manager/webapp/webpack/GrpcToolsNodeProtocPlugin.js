"use strict";

const os = require('os');
const path = require('path');
const fs = require('fs');
const glob = require('glob');
const execSync = require('child_process').execSync;

function clearOutputDirectory(directory) {
    if (fs.existsSync(directory)) {
        console.log(`Clearing output directory: ${directory}`);
        const files = fs.readdirSync(directory);
        for (const file of files) {
            const filePath = path.join(directory, file);
            console.log(`Deleting file: ${filePath}`);
            fs.unlinkSync(filePath);
        }
    } else {
        console.log(`Output directory does not exist, no need to clear: ${directory}`);
    }
}

class GrpcToolsNodeProtocPlugin {
    constructor(options) {
        this.protoPaths = options.protoPaths || []; 
        this.protoSources = options.protoSources || []; 
        this.outputDir = options.outputDir || './'; 
        console.log('GrpcToolsNodeProtocPlugin initialized with options:', options);
    }

    apply(compiler) {
        compiler.hooks.environment.tap('GrpcToolsNodeProtocPlugin', () => {
            try {
                console.log("Cleaning existing files, if any");
                clearOutputDirectory(this.outputDir);
                console.log(`Preparing to compile protocol buffers into ${this.outputDir}`);

                const protocPath = this.resolveProtocExecutable();
                const protocGenJsPath = this.resolveProtocGenJsExecutable();
                const resolvedProtoPaths = this.protoPaths.map(p => path.resolve(__dirname, p));
                console.log('Resolved proto paths:', resolvedProtoPaths);

                const resolvedProtoSources = this.protoSources.flatMap(s => glob.sync(path.resolve(__dirname, s)));
                console.log('Resolved proto sources:', resolvedProtoSources);

                const protocArgs = [
                    `--js_out=import_style=commonjs,binary:${this.outputDir}`,
                    `--plugin=protoc-gen-js=${protocGenJsPath}`,
                    ...resolvedProtoPaths.map(p => `--proto_path=${p}`),
                    ...resolvedProtoSources
                ];

                const command = `${protocPath} ${protocArgs.join(' ')}`;
                console.log('Executing command:', command);
                execSync(command, { stdio: 'inherit' });

                console.log('Protocol buffer compilation completed successfully');
            } catch (error) {
                console.error('Error running grpc tools', error);
            }
        });
    }

    resolveProtocExecutable() {
        const isWindows = os.platform() === 'win32';
        const protocDir = isWindows ? 'tools/protobuf/win64/bin' : 'tools/protobuf/linux-x86_64/bin';
        const protocFileName = isWindows ? 'protoc.exe' : 'protoc';
        return path.resolve(__dirname, '../../../..', protocDir, protocFileName);
    }

    resolveProtocGenJsExecutable() {
        const isWindows = os.platform() === 'win32';
        const protocGenJsDir = isWindows ? 'tools/protobuf-javascript/win64/bin' : 'tools/protobuf-javascript/linux-x86_64/bin';
        const protocGenJsFileName = isWindows ? 'protoc-gen-js.exe' : 'protoc-gen-js';
        return path.resolve(__dirname, '../../../..', protocGenJsDir, protocGenJsFileName);
    }
}

module.exports = GrpcToolsNodeProtocPlugin;
