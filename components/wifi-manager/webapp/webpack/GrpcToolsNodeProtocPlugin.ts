import { Compiler } from 'webpack';
const grpcTools = require('grpc-tools');
const { execSync } = require('child_process');
const path = require('path');
const fs = require('fs');
const glob = require('glob');

function clearOutputDirectory(directory:string) {
    if (fs.existsSync(directory)) {
        const files = fs.readdirSync(directory);
        for (const file of files) {
            const filePath = path.join(directory, file);
            fs.unlinkSync(filePath);
        }
    }
}
export = GrpcToolsNodeProtocPlugin;
// Define the interface for the plugin options
interface GrpcToolsNodeProtocPluginOptions {
    protoPaths?: string[];
    protoSources?: string[];
    outputDir?: string;
  }
  
  class GrpcToolsNodeProtocPlugin {
      private protoPaths: string[];
      private protoSources: string[];
      private outputDir: string;
  
      constructor(options: GrpcToolsNodeProtocPluginOptions) {
          this.protoPaths = options.protoPaths || []; // Array of proto_path directories
          this.protoSources = options.protoSources || []; // Array of proto source files or directories
          this.outputDir = options.outputDir || './'; // Output directory
      }

    apply(compiler:Compiler) {
        compiler.hooks.environment.tap('GrpcToolsNodeProtocPlugin', () => {
            try {
                console.log(`Cleaning existing files, if any`)
                clearOutputDirectory(this.outputDir);
                console.log(`Writing protocol buffer files into ${this.outputDir}`)

                // Resolve proto_path directories
                const resolvedProtoPaths = this.protoPaths.map(p => path.resolve(__dirname, p));

                var resolvedProtoSources:string[] = [];
                this.protoSources.forEach(function (s) {
                    var matches = glob.sync(path.resolve(__dirname, s));
                    resolvedProtoSources = resolvedProtoSources.concat(matches);
                });



                const protocArgs = [
                    `--js_out=import_style=commonjs,binary:${this.outputDir}`,
                    // `--grpc_out=generate_package_definition:${this.outputDir}`,
                    ...resolvedProtoPaths.map(p => `--proto_path=${p}`),
                    ...resolvedProtoSources
                ];
                
                const command = `npx grpc_tools_node_protoc ${protocArgs.join(' ')}`;
                execSync(command, { stdio: 'inherit' });
                
            } catch (error) {
                console.error('Error running grpc tools', error);
            }
        });
    }
}

