// First, checks if it isn't implemented yet.
if (!String.prototype.format) {
    String.prototype.format = function() {
        var args = arguments;
        return this.replace(/{(\d+)}/g, function(match, number) { 
            return typeof args[number] != 'undefined'
                ? args[number]
                : match
            ;
        });
    };
}

var releaseURL = 'https://api.github.com/repos/sle118/squeezelite-esp32/releases';
var recovery = false;
var enableAPTimer = true;
var enableStatusTimer = true;
var commandHeader = 'squeezelite -b 500:2000 -d all=info ';
var pname, ver, otapct, otadsc;
var blockAjax = false;
var blockFlashButton = false;
var lastMsg = '';

var apList = null;
var selectedSSID = "";
var refreshAPInterval = null; 
var checkStatusInterval = null;

var StatusIntervalActive = false;
var RefreshAPIIntervalActive = false;

var output = '';

function stopCheckStatusInterval(){
    if(checkStatusInterval != null){
        clearTimeout(checkStatusInterval);
        checkStatusInterval = null;
    }
    StatusIntervalActive = false;
}

function stopRefreshAPInterval(){
    if(refreshAPInterval != null){
        clearTimeout(refreshAPInterval);
        refreshAPInterval = null;
    }
    RefreshAPIIntervalActive = false;
}

function startCheckStatusInterval(){
    StatusIntervalActive = true;
    checkStatusInterval = setTimeout(checkStatus, 3000);
}

function startRefreshAPInterval(){
    RefreshAPIIntervalActive = true;
    refreshAPInterval = setTimeout(refreshAP, 2800);
}

function RepeatCheckStatusInterval(){
    if(StatusIntervalActive)
        startCheckStatusInterval();
}

function RepeatRefreshAPInterval(){
    if(RefreshAPIIntervalActive)
        startRefreshAPInterval();
}

$(document).ready(function(){
    $("#wifi-status").on("click", ".ape", function() {
        $( "#wifi" ).slideUp( "fast", function() {});
        $( "#connect-details" ).slideDown( "fast", function() {});
    });

    $("#manual_add").on("click", ".ape", function() {
        selectedSSID = $(this).text();
        $( "#ssid-pwd" ).text(selectedSSID);
        $( "#wifi" ).slideUp( "fast", function() {});
        $( "#connect_manual" ).slideDown( "fast", function() {});
        $( "#connect" ).slideUp( "fast", function() {});

        //update wait screen
        $( "#loading" ).show();
        $( "#connect-success" ).hide();
        $( "#connect-fail" ).hide();
    });

    $("#wifi-list").on("click", ".ape", function() {
        selectedSSID = $(this).text();
        $( "#ssid-pwd" ).text(selectedSSID);
        $( "#wifi" ).slideUp( "fast", function() {});
        $( "#connect_manual" ).slideUp( "fast", function() {});
        $( "#connect" ).slideDown( "fast", function() {});

        //update wait screen
        $( "#loading" ).show();
        $( "#connect-success" ).hide();
        $( "#connect-fail" ).hide();		
    });

    $("#cancel").on("click", function() {
        selectedSSID = "";
        $( "#connect" ).slideUp( "fast", function() {});
        $( "#connect_manual" ).slideUp( "fast", function() {});
        $( "#wifi" ).slideDown( "fast", function() {});
    });

    $("#manual_cancel").on("click", function() {
        selectedSSID = "";
        $( "#connect" ).slideUp( "fast", function() {});
        $( "#connect_manual" ).slideUp( "fast", function() {});
        $( "#wifi" ).slideDown( "fast", function() {});
    });

    $("#join").on("click", function() {
        performConnect();
    });

    $("#manual_join").on("click", function() {
        performConnect($(this).data('connect'));
    });

    $("#ok-details").on("click", function() {
        $( "#connect-details" ).slideUp( "fast", function() {});
        $( "#wifi" ).slideDown( "fast", function() {});

    });

    $("#ok-credits").on("click", function() {
        $( "#credits" ).slideUp( "fast", function() {});
        $( "#app" ).slideDown( "fast", function() {});
    });

    $("#acredits").on("click", function(event) {
        event.preventDefault();
        $( "#app" ).slideUp( "fast", function() {});
        $( "#credits" ).slideDown( "fast", function() {});
    });

    $("#ok-connect").on("click", function() {
        $( "#connect-wait" ).slideUp( "fast", function() {});
        $( "#wifi" ).slideDown( "fast", function() {});
    });

    $("#disconnect").on("click", function() {
        $( "#connect-details-wrap" ).addClass('blur');
        $( "#diag-disconnect" ).slideDown( "fast", function() {});
    });

    $("#no-disconnect").on("click", function() {
        $( "#diag-disconnect" ).slideUp( "fast", function() {});
        $( "#connect-details-wrap" ).removeClass('blur');
    });

    $("#yes-disconnect").on("click", function() {
        stopCheckStatusInterval();
        selectedSSID = "";

        $( "#diag-disconnect" ).slideUp( "fast", function() {});
        $( "#connect-details-wrap" ).removeClass('blur');

        $.ajax({
            url: '/connect.json',
            dataType: 'text',
            method: 'DELETE',
            cache: false,
            contentType: 'application/json; charset=utf-8',
            data: JSON.stringify({ 'timestamp': Date.now()})
            
        });

        startCheckStatusInterval();

        $( "#connect-details" ).slideUp( "fast", function() {});
        $( "#wifi" ).slideDown( "fast", function() {})
    });

    $("input#autoexec-cb").on("click", function() {
        var data = { 'timestamp': Date.now() };
        autoexec = (this.checked)?1:0;
        data['autoexec'] = autoexec;
        showMessage('please wait for the ESP32 to reboot', 'WARNING');
        $.ajax({
            url: '/config.json',
            dataType: 'text',
            method: 'POST',
            cache: false,
            headers: { "X-Custom-autoexec": autoexec },
            contentType: 'application/json; charset=utf-8',
            data: JSON.stringify(data),
            error: function (xhr, ajaxOptions, thrownError) {
                console.log(xhr.status);
                console.log(thrownError);
                if (thrownError != '') showMessage(thrownError, 'ERROR');
            },
            complete: function(response) {
                //var returnedResponse = JSON.parse(response.responseText);
                console.log(response.responseText);
                console.log('sent config JSON with headers:', autoexec);
                console.log('now triggering reboot');
                $.ajax({
                    url: '/reboot.json',
                    dataType: 'text',
                    method: 'POST',
                    cache: false,
                    contentType: 'application/json; charset=utf-8',
                    data: JSON.stringify({ 'timestamp': Date.now()}),
                    error: function (xhr, ajaxOptions, thrownError) {
                        console.log(xhr.status);
                        console.log(thrownError);
                        if (thrownError != '') showMessage(thrownError, 'ERROR');
                    },
                    complete: function(response) {
                    	console.log('reboot call completed');

                    }
                });
            }            
        });
    });

    $("input#save-autoexec1").on("click", function() {
        var data = { 'timestamp': Date.now() };
        autoexec1 = $("#autoexec1").val();
        data['autoexec1'] = autoexec1;

        $.ajax({
            url: '/config.json',
            dataType: 'text',
            method: 'POST',
            cache: false,
            headers: { "X-Custom-autoexec1": autoexec1 },
            contentType: 'application/json; charset=utf-8',
            data: JSON.stringify(data),
            error: function (xhr, ajaxOptions, thrownError) {
                console.log(xhr.status);
                console.log(thrownError);
                if (thrownError != '') showMessage(thrownError, 'ERROR');
            }
        });
        console.log('sent config JSON with headers:', autoexec1);
        console.log('sent data:', JSON.stringify(data));
    });

    $("input#save-gpio").on("click", function() {
        var data = { 'timestamp': Date.now() };
        var headers = {};
        $("input.gpio").each(function() {
            var id = $(this)[0].id;
            var pin = $(this).val();
            if (pin != '') {
                headers["X-Custom-"+id] = pin;
                data[id] = pin;
            }
        });
        $.ajax({
            url: '/config.json',
            dataType: 'text',
            method: 'POST',
            cache: false,
            headers: headers,
            contentType: 'application/json; charset=utf-8',
            data: JSON.stringify(data),
            error: function (xhr, ajaxOptions, thrownError) {
                console.log(xhr.status);
                console.log(thrownError);
                if (thrownError != '') showMessage(thrownError, 'ERROR');
            }
        });
        console.log('sent config JSON with headers:', JSON.stringify(headers));
        console.log('sent config JSON with data:', JSON.stringify(data));
    });

    $("#save-nvs").on("click", function() {
        var headers = {};
        var data = { 'timestamp': Date.now() };
        $("input.nvs").each(function() {
            var key = $(this)[0].id;
            var val = $(this).val();
            if (key != '') {
                headers["X-Custom-"+key] = val;
                data[key] = val;
            }
        });
        var key = $("#nvs-new-key").val();
        var val = $("#nvs-new-value").val();
        if (key != '') {
            headers["X-Custom-"+key] = val;
            data[key] = val;
        }
        $.ajax({
            url: '/config.json',
            dataType: 'text',
            method: 'POST',
            cache: false,
            headers: headers,
            contentType: 'application/json; charset=utf-8',
            data: JSON.stringify(data),
            error: function (xhr, ajaxOptions, thrownError) {
                console.log(xhr.status);
                console.log(thrownError);
                if (thrownError != '') showMessage(thrownError, 'ERROR');
            }
        });
        console.log('sent config JSON with headers:', JSON.stringify(headers));
        console.log('sent config JSON with data:', JSON.stringify(data));
    });

    $("#flash").on("click", function() {
        var data = { 'timestamp': Date.now() };
        if (blockFlashButton) return;
        blockFlashButton = true;
        var url = $("#fwurl").val();
        data['fwurl'] = url;
        $.ajax({
            url: '/config.json',
            dataType: 'text',
            method: 'POST',
            cache: false,
            headers: { "X-Custom-fwurl": url },
            contentType: 'application/json; charset=utf-8',
            data: JSON.stringify(data),
            error: function (xhr, ajaxOptions, thrownError) {
                console.log(xhr.status);
                console.log(thrownError);
                if (thrownError != '') showMessage(thrownError, 'ERROR');
            }
        });
        enableStatusTimer = true;
    });

    $("#generate-command").on("click", function() {
        var commandLine = commandHeader + '-n ' + $("#player").val();

        if (output == 'bt') {
            commandLine += ' -o "BT -n \'' + $("#btsink").val() + '\'" -R -Z 192000';
        } else if (output == 'spdif') {
            commandLine += ' -o SPDIF -R -Z 192000';
        } else {
            commandLine += ' -o I2S';
        }
        if ($("#optional").val() != '') {
            commandLine += ' ' + $("#optional").val();
        }
        $("#autoexec1").val(commandLine);
    });	

    $('[name=audio]').on("click", function(){
        if (this.id == 'bt') {
            $("#btsinkdiv").show(200);
            output = 'bt';
        } else if (this.id == 'spdif') {
            $("#btsinkdiv").hide(200);
            output = 'spdif';
        } else {
            $("#btsinkdiv").hide(200);
            output = 'i2s';
        }
    });

    $('#fwcheck').on("click", function(){
        $("#releaseTable").html("");
        $.getJSON(releaseURL, function(data) {
            var i=0;
            data.forEach(function(release) {
                var url = '';
                release.assets.forEach(function(asset) {
                    if (asset.name.match(/\.bin$/)) {
                        url = asset.browser_download_url;
                    }
                });
                var [ver, idf, cfg, branch] = release.name.split('#');
                var body = release.body;
                body = body.replace(/\'/ig, "\"");
                body = body.replace(/[\s\S]+(### Revision Log[\s\S]+)### ESP-IDF Version Used[\s\S]+/, "$1");
                body = body.replace(/- \(.+?\) /g, "- ");
                var [date, time] = release.created_at.split('T');
                var trclass = (i++ > 6)?' hide':'';
                $("#releaseTable").append(
                    "<tr class='release"+trclass+"'>"+
                        "<td data-toggle='tooltip' title='"+body+"'>"+ver+"</td>"+
                        "<td>"+date+"</td>"+
                        "<td>"+cfg+"</td>"+
                        "<td>"+idf+"</td>"+
                        "<td>"+branch+"</td>"+
                        "<td><input id='generate-command' type='button' class='btn btn-success' value='Select' data-url='"+url+"' onclick='setURL(this);' /></td>"+
                    "</tr>"
                );
            });
            if (i > 7) {
                $("#releaseTable").append(
                    "<tr id='showall'>"+
                        "<td colspan='6'>"+
                            "<input type='button' id='showallbutton' class='btn btn-info' value='Show older releases' />"+
                        "</td>"+
                    "</tr>"
                );
                $('#showallbutton').on("click", function(){
                    $("tr.hide").removeClass("hide");
                    $("tr#showall").addClass("hide");
                });
            }
            $("#searchfw").css("display", "inline");
        })
        .fail(function() {
            alert("failed to fetch release history!");
        });
    });

    $('input#searchinput').on("input", function(){
        var s = $('input#searchinput').val();
        var re = new RegExp(s, "gi");
        if (s.length == 0) {
            $("tr.release").removeClass("hide");
        } else if (s.length < 3) {
            $("tr.release").addClass("hide");
        } else {
            $("tr.release").addClass("hide");
            $("tr.release").each(function(tr){
                $(this).find('td').each (function() {
                    if ($(this).html().match(re)) {
                        $(this).parent().removeClass('hide');
                    }
                });
            });
        }
    });

    //first time the page loads: attempt to get the connection status and start the wifi scan
    refreshAP();
    getConfig();

    //start timers
    startCheckStatusInterval();
    startRefreshAPInterval();

    $('[data-toggle="tooltip"]').tooltip({
        html: true,
        placement : 'right',
    });
});

function setURL(button) {
    var url = button.dataset.url;
    $("#fwurl").val(url);

    $('[data-url^="http"]').addClass("btn-success").removeClass("btn-danger");
    $('[data-url="'+url+'"]').addClass("btn-danger").removeClass("btn-success");
}

function performConnect(conntype){
    //stop the status refresh. This prevents a race condition where a status 
    //request would be refreshed with wrong ip info from a previous connection
    //and the request would automatically shows as succesful.
    stopCheckStatusInterval();

    //stop refreshing wifi list
    stopRefreshAPInterval();

    var pwd;
    var dhcpname;
    if (conntype == 'manual') {
        //Grab the manual SSID and PWD
        selectedSSID=$('#manual_ssid').val();
        pwd = $("#manual_pwd").val();
        dhcpname= $("#dhcp-name2").val();;
    }else{
        pwd = $("#pwd").val();
        dhcpname= $("#dhcp-name1").val();;
    }
    //reset connection 
    $( "#loading" ).show();
    $( "#connect-success" ).hide();
    $( "#connect-fail" ).hide();

    $( "#ok-connect" ).prop("disabled",true);
    $( "#ssid-wait" ).text(selectedSSID);
    $( "#connect" ).slideUp( "fast", function() {});
    $( "#connect_manual" ).slideUp( "fast", function() {});
    $( "#connect-wait" ).slideDown( "fast", function() {});

    $.ajax({
        url: '/connect.json',
        dataType: 'text',
        method: 'POST',
        cache: false,
        headers: { 'X-Custom-ssid': selectedSSID, 'X-Custom-pwd': pwd, 'X-Custom-host_name': dhcpname },
        contentType: 'application/json; charset=utf-8',
        data: { 'timestamp': Date.now()},
        error: function (xhr, ajaxOptions, thrownError) {
            console.log(xhr.status);
            console.log(thrownError);
            if (thrownError != '') showMessage(thrownError, 'ERROR');
        }
    });

    //now we can re-set the intervals regardless of result
    startCheckStatusInterval();
    startRefreshAPInterval();
}

function rssiToIcon(rssi){
    if(rssi >= -60){
        return 'w0';
    }
    else if(rssi >= -67){
        return 'w1';
    }
    else if(rssi >= -75){
        return 'w2';
    }
    else{
        return 'w3';
    }
}

function refreshAP(){
    if (!enableAPTimer) return;
    $.getJSON( "/ap.json", function( data ) {
        if(data.length > 0){
            //sort by signal strength
            data.sort(function (a, b) {
                var x = a["rssi"]; var y = b["rssi"];
                return ((x < y) ? 1 : ((x > y) ? -1 : 0));
            });
            apList = data;
            refreshAPHTML(apList);
        }
    });
}

function refreshAPHTML(data){
    var h = "";
    data.forEach(function(e, idx, array) {
        h += '<div class="ape{0}"><div class="{1}"><div class="{2}">{3}</div></div></div>'.format(idx === array.length - 1?'':' brdb', rssiToIcon(e.rssi), e.auth==0?'':'pw',e.ssid);
        h += "\n";
    });

    $( "#wifi-list" ).html(h)
}

function checkStatus(){
    RepeatCheckStatusInterval();
    if (!enableStatusTimer) return;
    if (blockAjax) return;
    blockAjax = true;
    $.getJSON( "/status.json", function( data ) {
        if (data.hasOwnProperty('ssid') && data['ssid'] != ""){
            if (data["ssid"] === selectedSSID){
                //that's a connection attempt
                if (data["urc"] === 0){
                    //got connection
                    $("#connected-to span").text(data["ssid"]);
                    $("#connect-details h1").text(data["ssid"]);
                    $("#ip").text(data["ip"]);
                    $("#netmask").text(data["netmask"]);
                    $("#gw").text(data["gw"]);
                    $("#wifi-status").slideDown( "fast", function() {});
                    $("span#foot-wifi").html(", SSID: <strong>"+data["ssid"]+"</strong>, IP: <strong>"+data["ip"]+"</strong>");

                    //unlock the wait screen if needed
                    $( "#ok-connect" ).prop("disabled",false);

                    //update wait screen
                    $( "#loading" ).hide();
                    $( "#connect-success" ).append("<p>Your IP address now is: " + text(data["ip"]) + "</p>");
                    $( "#connect-success" ).show();
                    $( "#connect-fail" ).hide();

                    enableAPTimer = false;
                    if (!recovery) enableStatusTimer = false;
                }
                else if (data["urc"] === 1){
                    //failed attempt
                    $("#connected-to span").text('');
                    $("#connect-details h1").text('');
                    $("#ip").text('0.0.0.0');
                    $("#netmask").text('0.0.0.0');
                    $("#gw").text('0.0.0.0');
                    $("span#foot-wifi").html("");

                    //don't show any connection
                    $("#wifi-status").slideUp( "fast", function() {});

                    //unlock the wait screen
                    $( "#ok-connect" ).prop("disabled",false);

                    //update wait screen
                    $( "#loading" ).hide();
                    $( "#connect-fail" ).show();
                    $( "#connect-success" ).hide();

                    enableAPTimer = true;
                    enableStatusTimer = true;
                }
            }
            else if (data.hasOwnProperty('urc') && data['urc'] === 0){
                //ESP32 is already connected to a wifi without having the user do anything
                if( !($("#wifi-status").is(":visible")) ){
                    $("#connected-to span").text(data["ssid"]);
                    $("#connect-details h1").text(data["ssid"]);
                    $("#ip").text(data["ip"]);
                    $("#netmask").text(data["netmask"]);
                    $("#gw").text(data["gw"]);
                    $("#wifi-status").slideDown( "fast", function() {});
                    $("span#foot-wifi").html(", SSID: <strong>"+data["ssid"]+"</strong>, IP: <strong>"+data["ip"]+"</strong>");
                }
                enableAPTimer = false;
                if (!recovery) enableStatusTimer = false;
            }
        }
        else if (data.hasOwnProperty('urc') && data['urc'] === 2){
            //that's a manual disconnect
            if($("#wifi-status").is(":visible")){
                $("#wifi-status").slideUp( "fast", function() {});
                $("span#foot-wifi").html("");
            }
            enableAPTimer = true;
            enableStatusTimer = true;
        }
        if (data.hasOwnProperty('recovery')) {
            if (data["recovery"] === 1) {
                recovery = true;
                $("#otadiv").show();
                $('a[href^="#tab-audio"]').hide();
                $('a[href^="#tab-gpio"]').show();
                $('a[href^="#tab-nvs"]').show();
                $("footer.footer").removeClass('sl');
                $("footer.footer").addClass('recovery');
                $("#boot-button").html('Reboot');
                $("#boot-form").attr('action', '/reboot.json');
                enableStatusTimer = true;
            } else {
                recovery = false;
                $("#otadiv").hide();
                $('a[href^="#tab-audio"]').show();
                $('a[href^="#tab-gpio"]').hide();
                $('a[href^="#tab-nvs"]').hide();
                $("footer.footer").removeClass('recovery');
                $("footer.footer").addClass('sl');
                $("#boot-button").html('Recovery');
                $("#boot-form").attr('action', '/recovery.json');
                enableStatusTimer = false;
            }
        }
        if (data.hasOwnProperty('project_name') && data['project_name'] != ''){
            pname = data['project_name'];
        }
        if (data.hasOwnProperty('version') && data['version'] != ''){
            ver = data['version'];
            $("span#foot-fw").html("fw: <strong>"+ver+"</strong>, mode: <strong>"+pname+"</strong>");
        }
        if (data.hasOwnProperty('ota_pct') && data['ota_pct'] != 0){
            otapct = data['ota_pct'];
            $('.progress-bar').css('width', otapct+'%').attr('aria-valuenow', otapct);
            $('.progress-bar').html(otapct+'%');
        }
        if (data.hasOwnProperty('ota_dsc') && data['ota_dsc'] != ''){
            otadsc = data['ota_dsc'];
            $("span#flash-status").html(otadsc);
            if (otadsc.match(/Error:/) || otapct > 95) {
                blockFlashButton = false;
                enableStatusTimer = true;
            }
        } else {
            $("span#flash-status").html('');
        }
        if (data.hasOwnProperty('message') && data['message'] != ''){
            var msg = data['message'].text;
            var severity = data['message'].severity;
            if (msg != lastMsg) {
                showMessage(msg, severity);
                lastMsg = msg;
            }
        }
        blockAjax = false;
    })
    .fail(function(xhr, ajaxOptions, thrownError) {
        console.log(xhr.status);
        console.log(thrownError);
        if (thrownError != '') showMessage(thrownError, 'ERROR');
        blockAjax = false;
    });
}

function getConfig() {
    $.getJSON("/config.json", function(data) {
        Object.keys(data).sort().forEach(function(key, i) {
            if (data.hasOwnProperty(key)) {
                if (key == 'autoexec') {
                    if (data["autoexec"] === "1") {
                        $("#autoexec-cb")[0].checked=true;
                    } else {
                        $("#autoexec-cb")[0].checked=false;
                    }
                } else if (key == 'autoexec1') {
                    $("textarea#autoexec1").val(data[key]);
                } else if (key == 'host_name') {
                    $("dhcp-name1").val(data[key]);
                    $("dhcp-name2").val(data[key]);
                }

                $("tbody#nvsTable").append(
                    "<tr>"+
                        "<td>"+key+"</td>"+
                        "<td class='value'>"+
                            "<input type='text' class='form-control nvs' id='"+key+"'>"+
                        "</td>"+
                    "</tr>"
                );
                $("input#"+key).val(data[key]);
            }
        });
        $("tbody#nvsTable").append(
            "<tr>"+
                "<td>"+
                    "<input type='text' class='form-control' id='nvs-new-key' placeholder='new key'>"+
                "</td>"+
                "<td>"+
                    "<input type='text' class='form-control' id='nvs-new-value' placeholder='new value'>"+
                "</td>"+
            "</tr>"
        );
    })
    .fail(function(xhr, ajaxOptions, thrownError) {
        console.log(xhr.status);
        console.log(thrownError);
        if (thrownError != '') showMessage(thrownError, 'ERROR');
        blockAjax = false;
    });
}

function showMessage(message, severity) {
    if (severity == 'INFO') {
        $('#message').css('background', '#6af');
    } else if (severity == 'WARNING') {
        $('#message').css('background', '#ff0');
    } else {
        $('#message').css('background', '#f00');
    }
    $('#message').html(message);
    $("#content").fadeTo("slow", 0.3, function() {
        $("#message").show(500).delay(5000).hide(500, function() {
            $("#content").fadeTo("slow", 1.0);
        });
    });
}
