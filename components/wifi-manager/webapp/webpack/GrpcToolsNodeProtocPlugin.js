"use strict";
var __spreadArray = (this && this.__spreadArray) || function (to, from, pack) {
    if (pack || arguments.length === 2) for (var i = 0, l = from.length, ar; i < l; i++) {
        if (ar || !(i in from)) {
            if (!ar) ar = Array.prototype.slice.call(from, 0, i);
            ar[i] = from[i];
        }
    }
    return to.concat(ar || Array.prototype.slice.call(from));
};
var grpcTools = require('grpc-tools');
var execSync = require('child_process').execSync;
var path = require('path');
var fs = require('fs');
var glob = require('glob');
function clearOutputDirectory(directory) {
    if (fs.existsSync(directory)) {
        var files = fs.readdirSync(directory);
        for (var _i = 0, files_1 = files; _i < files_1.length; _i++) {
            var file = files_1[_i];
            var filePath = path.join(directory, file);
            fs.unlinkSync(filePath);
        }
    }
}
var GrpcToolsNodeProtocPlugin = /** @class */ (function () {
    function GrpcToolsNodeProtocPlugin(options) {
        this.protoPaths = options.protoPaths || []; // Array of proto_path directories
        this.protoSources = options.protoSources || []; // Array of proto source files or directories
        this.outputDir = options.outputDir || './'; // Output directory
    }
    GrpcToolsNodeProtocPlugin.prototype.apply = function (compiler) {
        var _this = this;
        compiler.hooks.environment.tap('GrpcToolsNodeProtocPlugin', function () {
            try {
                console.log("Cleaning existing files, if any");
                clearOutputDirectory(_this.outputDir);
                console.log("Writing protocol buffer files into ".concat(_this.outputDir));
                // Resolve proto_path directories
                var resolvedProtoPaths = _this.protoPaths.map(function (p) { return path.resolve(__dirname, p); });
                var resolvedProtoSources = [];
                _this.protoSources.forEach(function (s) {
                    var matches = glob.sync(path.resolve(__dirname, s));
                    resolvedProtoSources = resolvedProtoSources.concat(matches);
                });
                var protocArgs = __spreadArray(__spreadArray([
                    "--js_out=import_style=commonjs,binary:".concat(_this.outputDir)
                ], resolvedProtoPaths.map(function (p) { return "--proto_path=".concat(p); }), true), resolvedProtoSources, true);
                var command = "npx grpc_tools_node_protoc ".concat(protocArgs.join(' '));
                execSync(command, { stdio: 'inherit' });
            }
            catch (error) {
                console.error('Error running grpc tools', error);
            }
        });
    };
    return GrpcToolsNodeProtocPlugin;
}());
module.exports = GrpcToolsNodeProtocPlugin;
