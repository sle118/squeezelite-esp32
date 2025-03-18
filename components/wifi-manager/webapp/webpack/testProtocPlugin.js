const path = require('path');
const GrpcToolsNodeProtocPlugin = require('./GrpcToolsNodeProtocPlugin.js');
const glob = require('glob');

// Define the paths as they are in your Webpack script
const buildRootPath = path.join(process.cwd(), '..', '..', '..');
const ComponentsPath = glob.sync(path.join(buildRootPath, 'components/'))[0];
const buildCRootPath = glob.sync(buildRootPath)[0];

// Instantiate the plugin
const plugin = new GrpcToolsNodeProtocPlugin({
    protoPaths: [
        path.join(ComponentsPath, 'spotify/cspot/bell/external/nanopb/generator/proto'),
        path.join(buildCRootPath, 'protobuf/proto')
    ],
    protoSources: [
        path.join(buildCRootPath, 'protobuf/proto/*.proto'),
        path.join(ComponentsPath, 'spotify/cspot/bell/external/nanopb/generator/proto/*.proto')
    ],
    outputDir: './src/js/proto'
});

// Manually execute the plugin
plugin.apply({
    hooks: {
        environment: {
            tap: (name, fn) => fn()
        }
    }
});

console.log('GrpcToolsNodeProtocPlugin test executed.');
