//https://github.com/nkolban/esp32-snippets/blob/master/tools/sdkconfig_compare/sdkconfig_compare.js
// Node.js application for comparing two ESP-IDF configuration files (sdkconfig)
const fs = require("fs");            // Require the file system processing library
const readline = require("readline");      // Require the readline processing library

var map_types = {
	"UNSET": 0,
	"VALUE": 1,
	"COMMENT": 2
};


// buildMap
// Read the sdkconfig file specified by fileName and produce a map of the name/value pairs contained
// within.  A Promise is returned that is fulfilled when the file has been read.
function buildMap(fileName) {
	const promise = new Promise(function (resolve, reject) {
		var readStream = fs.createReadStream(fileName);
		readStream.on("error", (err) => {
			reject(err);
		});
		const map = {};
		const lines_index = [];
		var lineNo = 1;

		const lineReader = readline.createInterface({
			input: readStream,
			crlfDelay: Infinity
		});
		
		// Called when a new line has been read from the file.
		lineReader.on("line", (line) => {
			line = line.trim();              // Trim whitespace from the line.

			if (line.length == 0) {          // Ignore empty lines
				return;
			}
			
			// split lines using named capture group regex, and loop through the resulting array
			var regexp = /^\s*[#]\s*(?<unset_key>CONFIG[^\s]*) is not set\s*$|^\s*(?<comment>[#].*)$|^\s*(?<key>[^#]{1}[^\s=]*)=(?<value>.*)$/gm;
			var match = regexp.exec(line);
			var key="";
			var map_entry={};
			while (match != null) {
				if (match.groups.unset_key) {
					key = match.groups.unset_key;
					map_entry = {
						key:key,
						type: map_types.UNSET,
						value: undefined,
						index: lineNo++,
					};

				} else if (match.groups.comment) {
					// accumulate comments in an array
					map_entry={
						type: map_types.COMMENT,
						value: match.groups.comment,
						index: lineNo++,
					};

				}
				else {
					key = match.groups.key;
					map_entry = {
						key:key,
						type: map_types.VALUE,
						value: match.groups.value,
						index: lineNo++,
					};
				}
				if(map_entry.type!=map_types.COMMENT){
					map[key] = map_entry;
				}
				lines_index.push(map_entry);
				match = regexp.exec(line);
			}
		}); // on(line)

		// Called when all the lines from the file have been consumed.
		lineReader.on("close", () => {
			resolve({map, lines_index});
		}); // on(close)

	});
	return promise;
} // buildMap


const args = process.argv;
if (args.length != 3) {
	console.log("Usage: node sdkconfig_compare file1 ");
	process.exit();
}
const sdkconfig = "./sdkconfig";
const comparedFile = args[2];

buildMap(sdkconfig).then((sdkConfigResult ) => {
	var sdkconfigMap = sdkConfigResult.map;
	var sdkconfigLines = sdkConfigResult.lines_index;
	buildMap(comparedFile).then((comparedResult) => {
		var comparedFileMap = comparedResult.map;
		var comparedLines = comparedResult.lines_index;
		buildMap("./sdkconfig.defaults").then((sdkconfigResults) => {
			var sdkconfig_defaults = sdkconfigResults.map;
			var sdkconfig_defaults_lines = sdkconfigResults.lines_index;

			// Three passes
			// In A and not B
			// in B and not A
			// value different in A and B
			var newmap = {};
			var entry={};
			console.log(`\n\n${sdkconfig} properties that are missing in ${comparedFile}\n**************************`);
			for (const prop in sdkconfigMap) {
				 entry = sdkconfigMap[prop];
				if(entry.type==map_types.COMMENT || entry.type== map_types.UNSET) continue;
				if (!comparedFileMap.hasOwnProperty(prop) && !sdkconfig_defaults.hasOwnProperty(prop)) {
					newmap[prop] = entry;
					console.log(`${prop}=${entry.value}`);
				}
				else if(comparedFileMap.hasOwnProperty(prop)){
					newmap[prop] =entry;
				}

			}
			console.log(`\n\n${comparedFile} properties that are missing in ${sdkconfig}\n**************************`);
			for (const prop in comparedFileMap) {
				entry = comparedFileMap[prop];
				if(entry.type==map_types.COMMENT || entry.type== map_types.UNSET) continue;
				if (comparedFileMap.hasOwnProperty(prop)) {
					if (!sdkconfigMap.hasOwnProperty(prop) && !sdkconfig_defaults.hasOwnProperty(prop)) {
						console.log(`${prop}=${entry.value}`);
					}
				}
			}
			console.log(`\n\nproperties that are different ${sdkconfig}<->${comparedFile} \n**************************`);
			for (const prop in sdkconfigMap) {
				entry = sdkconfigMap[prop];
				if(entry.type==map_types.COMMENT) continue;
				if (sdkconfigMap.hasOwnProperty(prop)) {
					if (comparedFileMap.hasOwnProperty(prop)) {
						if (entry.value != comparedFileMap[prop].value) {
							console.log(`${prop} : [${entry.value}] != [${comparedFileMap[prop].value}]`);
						}
					}
				}
			}
			var newlines = [];
			for(const prop in newmap){
				newlines.splice(newmap[prop].index,0, `${prop}=${newmap[prop].value}`);
			}
			comparedLines.forEach(line=>{
				if(line.type==map_types.COMMENT ) {
					newlines.splice(line.index,0, line.value);
				}
				else if(line.type==map_types.UNSET){
					newlines.splice(line.index,0, `# ${line.key} is not set`);
				}
			});
			
			console.log(`\n\n${comparedFile} with missing properties\n**************************`);
			newlines.forEach(line => {
				console.log(line);
			});

		}).catch((err) => {
			console.log(err);
			process.exit();
		});
	}).catch((err) => {
		console.log(err);
		process.exit();
	});
}).catch((err) => {
	console.log(err);
	process.exit();
}
);
