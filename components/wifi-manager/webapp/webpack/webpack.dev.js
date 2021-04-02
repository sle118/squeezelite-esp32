/* eslint-disable  */
var path = require('path');
const merge = require('webpack-merge');
const common = require('./webpack.common.js');
const bodyParser = require('body-parser')
const MiniCssExtractPlugin = require('mini-css-extract-plugin');
const { config } = require('process');
const HtmlWebPackPlugin = require('html-webpack-plugin');
const SpriteLoaderPlugin = require('svg-sprite-loader/plugin');
const { Command } = require('commander');
let  cmdLines= { };
const data = {
    messages: require("../mock/messages.json"),
    messagequeue: require("../mock/messages.json"),
    commands: require("../mock/commands.json"),
    scan: require("../mock/scan.json"),
    ap: require("../mock/ap.json"),
    config: require("../mock/config.json"),
    statusdefinition: require("../mock/statusdefinition.json"),
    status: require("../mock/status.json")
};
const messagingTypes= {
	MESSAGING_INFO : 'MESSAGING_INFO',
	MESSAGING_WARNING : 'MESSAGING_WARNING',
	MESSAGING_ERROR : 'MESSAGING_ERROR'
};
const messagingClass= {
	MESSAGING_CLASS_OTA : 'MESSAGING_CLASS_OTA',
	MESSAGING_CLASS_SYSTEM : 'MESSAGING_CLASS_SYSTEM',
	MESSAGING_CLASS_STATS : 'MESSAGING_CLASS_STATS',
	MESSAGING_CLASS_CFGCMD: 'MESSAGING_CLASS_CFGCMD',
	MESSAGING_CLASS_BT: 'MESSAGING_CLASS_BT'
} ;
function requeueMessages(){
    data.messagequeue.push(...data.messages);
    console.log(`Re-queued ${data.messages.length} messages. Total queue length is: ${data.messagequeue.length}`);
}
function sendMessaging(cmdname,msgtype,msgClass,msg){
    let message_txt=`${cmdname}\n${msg}`;
    var d = new Date();
    var n = d.getMilliseconds();
    data.messagequeue.push({
        message:	message_txt,
        type:	msgtype,
        class:	msgClass,
        sent_time:	n,
        current_time:	n});
    console.log(`Queued message ~${data.messagequeue.length} type ${msgtype}, class ${msgClass}: ${message_txt}`);
}
Array.prototype.filter = function(fun /*, thisp*/) {
    var len = this.length >>> 0;
    if (typeof fun != "function")
      throw new TypeError();

    var res = [];
    var thisp = arguments[1];
    for (var i = 0; i < len; i++) {
      if (i in this) {
        var val = this[i];
        if (fun.call(thisp, val, i, this))
          res.push(val);
      }
    }
    return res;
  };
for(const cmdIdx in data.commands.commands){
    const cmd = data.commands.commands[cmdIdx];
    //console.log(`Creating command structure for ${cmd.name}`);
    cmdLines[cmd.name] = {
        cmd: new Command(),
    };
    cmdLines[cmd.name].cmd
        .storeOptionsAsProperties(false)
        .name(cmd.name)
        .exitOverride();
    for(const argIdx in cmd.argtable){
        const arg=cmd.argtable[argIdx];
        const optstr=((arg.shortopts?'-'+arg.shortopts:'')+ 
        (arg.shortopts && arg.longopts?', ':'')+
        (arg.longopts?'--'+arg.longopts:'') + 
        (arg.hasvalue?`${(arg.shortopts || arg.longopts?' ':'')}<${arg.datatype.replace(/[<>]/gm,'')}>`:''));
        //console.log(`   Option: ${optstr}, Glossary: ${arg.glossary}`);
        if(arg.mincount>0){
            cmdLines[cmd.name].cmd.requiredOption( optstr,arg.glossary);
        }
        else {
            cmdLines[cmd.name].cmd.option( optstr,arg.glossary);
        }
    }
}
const connectReturnCode = {
    UPDATE_CONNECTION_OK : 0, 
      UPDATE_FAILED_ATTEMPT : 1,
      UPDATE_USER_DISCONNECT : 2,
      UPDATE_LOST_CONNECTION : 3,
      UPDATE_FAILED_ATTEMPT_AND_RESTORE : 4
  }
module.exports = merge(common, {
    mode: 'development',
    devtool: 'inline-source-map',
    entry: {
        test: './src/test.ts'
    },    
    devServer: {
        contentBase: path.join(__dirname, 'dist'),
        publicPath: '/',
        port: 9100,
        host: '127.0.0.1',//your ip address
        disableHostCheck: true,
        overlay: true,

        before: function(app) {
            app.use(bodyParser.json()) // for parsing application/json
            app.use(bodyParser.urlencoded({ extended: true })) // for parsing application/x-www-form-urlencoded
            app.get('/ap.json', function(req, res) { res.json( data.ap ); });
            app.get('/scan.json', function(req, res) { res.json( data.scan ); });
            app.get('/config.json', function(req, res) { res.json( data.config ); });
            app.get('/status.json', function(req, res) { res.json( data.status ); });
            app.get('/messages.json', function(req, res) { 
                res.json( data.messagequeue ) ; 
                data.messagequeue=[];
            });
            
            app.get('/statusdefinition.json', function(req, res) { res.json( data.statusdefinition ); });
            app.get('/commands.json', function(req, res) { res.json( data.commands ); });
            app.post('/commands.json', function(req, res) { 
                console.log(req.body.command);
                try {
                    const cmdName=req.body.command.split(" ")[0];
                    const args=('node '+req.body.command).split(" ");
                    let cmd=cmdLines[cmdName].cmd;
                    if(cmd){
                        cmd.parse(args);
                        const msg=`Received Options: ${JSON.stringify(cmd.opts())}\n`;
                        console.log('Options: ', cmd.opts());
                        console.log('Remaining arguments: ', cmd.args);
                        sendMessaging(cmdName,messagingTypes.MESSAGING_INFO,messagingClass.MESSAGING_CLASS_CFGCMD,msg);
                    }                    
                } catch (error) {
                    console.error(error);
                }
                res.json( { 'Result' : 'Success' } ); 
            });
            app.post('/config.json', function(req, res) { 
                console.log(req.body);
                console.log(data.config);
                for (const property in req.body.config) {
                    console.log(`${property}: ${req.body.config[property].value}`);
                    if(data.config[property]=== undefined){
                        console.log(`Added config value ${property} [${req.body.config[property].value}]`);
                        data.config[property] = {value: req.body.config[property].value};
                    }
                    else if (data.config[property].value!=req.body.config[property].value){
                        console.log(`Updated config value ${property}\nFrom: ${data.config[property].value}\nTo: ${req.body.config[property].value}]`);
                        data.config[property].value=req.body.config[property].value;
                    }
                  }
                res.json( {} ); 
            });
            app.post('/status.json', function(req, res) { 

                for (const property in req.body.status) {
                    if(data.status[property]=== undefined){
                        console.log(`Added status value ${property} [${req.body.status[property]}]`);
                        data.status[property] = {value: req.body.status[property]};
                    }
                    else if (data.status[property]!==req.body.status[property]){
                        console.log(`Updated status value ${property}\nFrom: ${data.status[property]}\nTo: ${req.body.status[property]}`);
                        data.status[property]=req.body.status[property];
                    }
                  }
                res.json( {} ); 
            });            
            app.post('/connect.json', function(req, res) { 
                setTimeout(function(r){
                if(r.body.ssid.search('fail')>=0){
                    if(data.status.ssid){
                        // in this case, the same ssid will be reused - the ESP32 would restore its previous state on failure
                        data.status.urc=connectReturnCode.UPDATE_FAILED_ATTEMPT_AND_RESTORE;
                    }
                    else {
                        data.status.urc=connectReturnCode.UPDATE_FAILED_ATTEMPT;
                    }
                }
                else {
                    data.status.ssid=r.body.ssid;
                    data.status.urc=connectReturnCode.UPDATE_CONNECTION_OK;
                }
                }, 1000, req);
                res.json( {} );
             });
            app.post('/reboot_ota.json', function(req, res) { 
                data.status.recovery=0;
                requeueMessages();
                res.json( {} ); 
            });
            app.post('/reboot.json', function(req, res) { 
                res.json( {} ); 
                requeueMessages();
            });
            app.post('/recovery.json', function(req, res) { 
                data.status.recovery=1;
                requeueMessages();
                res.json( { } ); 
            });
            app.post('/flash.json', function(req, res) { 
                if(data.status.recovery>0){
                    res.json({});
                  }
                  else {
                    res.status(404).end(); 
                  }  
            });                  
            app.delete('/connect.json', function(req, res) { 
                data.status.ssid='';
                res.json( {} ); });
            app.get('/reboot', function(req, res) { res.json( {} ); });
        },
    },
    plugins: [
        new MiniCssExtractPlugin({
            filename: 'css/[name].css',
            chunkFilename: 'css/[id].css' ,
        }),
        new HtmlWebPackPlugin({
            title: 'SqueezeESP32-test',
            template: './src/test.ejs',
            filename: 'test',
            minify: false,
            excludeChunks: ['index'],
        }),        
        new SpriteLoaderPlugin({plainSprite: true})

    ],
});
