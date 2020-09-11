// First, checks if it isn't implemented yet.
if (!String.prototype.format) {
	String.prototype.format = function() {
		var args = arguments;
		return this.replace(/{(\d+)}/g, function(match, number) {
			return typeof args[number] != 'undefined' ?
				args[number] :
				match;
		});
	};
}
var nvs_type_t = {
	NVS_TYPE_U8: 0x01,
	/*!< Type uint8_t */
	NVS_TYPE_I8: 0x11,
	/*!< Type int8_t */
	NVS_TYPE_U16: 0x02,
	/*!< Type uint16_t */
	NVS_TYPE_I16: 0x12,
	/*!< Type int16_t */
	NVS_TYPE_U32: 0x04,
	/*!< Type uint32_t */
	NVS_TYPE_I32: 0x14,
	/*!< Type int32_t */
	NVS_TYPE_U64: 0x08,
	/*!< Type uint64_t */
	NVS_TYPE_I64: 0x18,
	/*!< Type int64_t */
	NVS_TYPE_STR: 0x21,
	/*!< Type string */
	NVS_TYPE_BLOB: 0x42,
	/*!< Type blob */
	NVS_TYPE_ANY: 0xff /*!< Must be last */
};

var task_state_t = {
	0: "eRunning",
	/*!< A task is querying the state of itself, so must be running. */
	1: "eReady",
	/*!< The task being queried is in a read or pending ready list. */
	2: "eBlocked",
	/*!< The task being queried is in the Blocked state. */
	3: "eSuspended",
	/*!< The task being queried is in the Suspended state, or is in the Blocked state with an infinite time out. */
	4: "eDeleted"

}
var escapeHTML = function(unsafe) {
	return unsafe.replace(/[&<"']/g, function(m) {
		switch (m) {
			case '&':
				return '&amp;';
			case '<':
				return '&lt;';
			case '"':
				return '&quot;';
			default:
				return '&#039;';
		}
	});
};
var releaseURL = 'https://api.github.com/repos/sle118/squeezelite-esp32/releases';
var recovery = false;
var enableAPTimer = true;
var enableStatusTimer = true;
var commandHeader = 'squeezelite -b 500:2000 -d all=info -C 30 -W';
var pname, ver, otapct, otadsc;
var blockAjax = false;
var blockFlashButton = false;
var dblclickCounter = 0;
var apList = null;
var selectedSSID = "";
var refreshAPInterval = null;
var checkStatusInterval = null;

var StatusIntervalActive = false;
var RefreshAPIIntervalActive = false;
var LastRecoveryState = null;
var LastCommandsState = null;
var output = '';

function delay_msg(t, v) {
	return new Promise(function(resolve) {
		setTimeout(resolve.bind(null, v), t)
	});
}

Promise.prototype.delay = function(t) {
	return this.then(function(v) {
		return delay_msg(t, v);
	});
}

function stopCheckStatusInterval() {
	if (checkStatusInterval != null) {
		clearTimeout(checkStatusInterval);
		checkStatusInterval = null;
	}
	StatusIntervalActive = false;
}

function stopRefreshAPInterval() {
	if (refreshAPInterval != null) {
		clearTimeout(refreshAPInterval);
		refreshAPInterval = null;
	}
	RefreshAPIIntervalActive = false;
}

function startCheckStatusInterval() {
	StatusIntervalActive = true;
	checkStatusInterval = setTimeout(checkStatus, 3000);
}

function startRefreshAPInterval() {
	RefreshAPIIntervalActive = true;
	refreshAPInterval = setTimeout(refreshAP(false), 4500); // leave enough time for the initial scan
}

function RepeatCheckStatusInterval() {
	if (StatusIntervalActive)
		startCheckStatusInterval();
}

function RepeatRefreshAPInterval() {
	if (RefreshAPIIntervalActive)
		startRefreshAPInterval();
}

function getConfigJson(slimMode) {
	var config = {};
	$("input.nvs").each(function() {
		var key = $(this)[0].id;
		var val = $(this).val();
		if (!slimMode) {
			var nvs_type = parseInt($(this)[0].attributes.nvs_type.nodeValue, 10);
			if (key != '') {
				config[key] = {};
				if (nvs_type == nvs_type_t.NVS_TYPE_U8 ||
					nvs_type == nvs_type_t.NVS_TYPE_I8 ||
					nvs_type == nvs_type_t.NVS_TYPE_U16 ||
					nvs_type == nvs_type_t.NVS_TYPE_I16 ||
					nvs_type == nvs_type_t.NVS_TYPE_U32 ||
					nvs_type == nvs_type_t.NVS_TYPE_I32 ||
					nvs_type == nvs_type_t.NVS_TYPE_U64 ||
					nvs_type == nvs_type_t.NVS_TYPE_I64) {
					config[key].value = parseInt(val);
				} else {
					config[key].value = val;
				}
				config[key].type = nvs_type;
			}
		} else {
			config[key] = val;
		}
	});
	var key = $("#nvs-new-key").val();
	var val = $("#nvs-new-value").val();
	if (key != '') {
		if (!slimMode) {
			config[key] = {};
			config[key].value = val;
			config[key].type = 33;
		} else {
			config[key] = val;
		}
	}
	return config;
}

function onFileLoad(elementId, event) {
	var data = {};
	try {
		data = JSON.parse(elementId.srcElement.result);
	} catch (e) {
		alert('Parsing failed!\r\n ' + e);
	}
	$("input.nvs").each(function() {
		var key = $(this)[0].id;
		var val = $(this).val();
		if (data[key]) {
			if (data[key] != val) {
				console.log("Changed " & key & " " & val & "==>" & data[key]);
				$(this).val(data[key]);
			}
		} else {
			console.log("Value " & key & " missing from file");
		}
	});

}

function onChooseFile(event, onLoadFileHandler) {
	if (typeof window.FileReader !== 'function')
		throw ("The file API isn't supported on this browser.");
	input = event.target;
	if (!input)
		throw ("The browser does not properly implement the event object");
	if (!input.files)
		throw ("This browser does not support the `files` property of the file input.");
	if (!input.files[0])
		return undefined;
	file = input.files[0];
	fr = new FileReader();
	fr.onload = onLoadFileHandler;
	fr.readAsText(file);
	input.value = "";
}
$(document).ready(function() {
	$("input#show-commands")[0].checked = LastCommandsState == 1 ? true : false;
	$('a[href^="#tab-commands"]').hide();
	$("#load-nvs").click(function() {
		$("#nvsfilename").trigger('click');
	});
	$("#wifi-status").on("click", ".ape", function() {
		$("#wifi").slideUp("fast", function() {});
		$("#connect-details").slideDown("fast", function() {});
	});

	$("#manual_add").on("click", ".ape", function() {
		selectedSSID = $(this).text();
		$("#ssid-pwd").text(selectedSSID);
		$("#wifi").slideUp("fast", function() {});
		$("#connect_manual").slideDown("fast", function() {});
		$("#connect").slideUp("fast", function() {});

		//update wait screen
		$("#loading").show();
		$("#connect-success").hide();
		$("#connect-fail").hide();
	});

	$("#wifi-list").on("click", ".ape", function() {
		selectedSSID = $(this).text();
		$("#ssid-pwd").text(selectedSSID);
		$("#wifi").slideUp("fast", function() {});
		$("#connect_manual").slideUp("fast", function() {});
		$("#connect").slideDown("fast", function() {});

		//update wait screen
		$("#loading").show();
		$("#connect-success").hide();
		$("#connect-fail").hide();
	});

	$("#cancel").on("click", function() {
		selectedSSID = "";
		$("#connect").slideUp("fast", function() {});
		$("#connect_manual").slideUp("fast", function() {});
		$("#wifi").slideDown("fast", function() {});
	});

	$("#manual_cancel").on("click", function() {
		selectedSSID = "";
		$("#connect").slideUp("fast", function() {});
		$("#connect_manual").slideUp("fast", function() {});
		$("#wifi").slideDown("fast", function() {});
	});

	$("#join").on("click", function() {
		performConnect();
	});

	$("#manual_join").on("click", function() {
		performConnect($(this).data('connect'));
	});

	$("#ok-details").on("click", function() {
		$("#connect-details").slideUp("fast", function() {});
		$("#wifi").slideDown("fast", function() {});

	});

	$("#ok-credits").on("click", function() {
		$("#credits").slideUp("fast", function() {});
		$("#app").slideDown("fast", function() {});
	});

	$("#acredits").on("click", function(event) {
		event.preventDefault();
		$("#app").slideUp("fast", function() {});
		$("#credits").slideDown("fast", function() {});
	});

	$("#ok-connect").on("click", function() {
		$("#connect-wait").slideUp("fast", function() {});
		$("#wifi").slideDown("fast", function() {});
	});

	$("#disconnect").on("click", function() {
		$("#connect-details-wrap").addClass('blur');
		$("#diag-disconnect").slideDown("fast", function() {});
	});

	$("#no-disconnect").on("click", function() {
		$("#diag-disconnect").slideUp("fast", function() {});
		$("#connect-details-wrap").removeClass('blur');
	});

	$("#yes-disconnect").on("click", function() {
		stopCheckStatusInterval();
		selectedSSID = "";

		$("#diag-disconnect").slideUp("fast", function() {});
		$("#connect-details-wrap").removeClass('blur');

		$.ajax({
			url: '/connect.json',
			dataType: 'text',
			method: 'DELETE',
			cache: false,
			contentType: 'application/json; charset=utf-8',
			data: JSON.stringify({
				'timestamp': Date.now()
			})

		});

		startCheckStatusInterval();

		$("#connect-details").slideUp("fast", function() {});
		$("#wifi").slideDown("fast", function() {})
	});

	$("input#show-commands").on("click", function() {
		this.checked = this.checked ? 1 : 0;
		if (this.checked) {
			$('a[href^="#tab-commands"]').show();
			LastCommandsState = 1;
		} else {
			LastCommandsState = 0;
			$('a[href^="#tab-commands"]').hide();
		}
	});

	$("input#show-nvs").on("click", function() {
		this.checked = this.checked ? 1 : 0;
		if (this.checked) {
			$('a[href^="#tab-nvs"]').show();
		} else {
			$('a[href^="#tab-nvs"]').hide();
		}

	});

	$("input#autoexec-cb").on("click", function() {
		var data = {
			'timestamp': Date.now()
		};
		autoexec = (this.checked) ? "1" : "0";
		data['config'] = {};
		data['config'] = {
			autoexec: {
				value: autoexec,
				type: 33
			}
		}

		showMessage('please wait for the ESP32 to reboot', 'MESSAGING_WARNING');
		$.ajax({
			url: '/config.json',
			dataType: 'text',
			method: 'POST',
			cache: false,
			//            headers: { "X-Custom-autoexec": autoexec },
			contentType: 'application/json; charset=utf-8',
			data: JSON.stringify(data),

			error: function(xhr, ajaxOptions, thrownError) {
				console.log(xhr.status);
				console.log(thrownError);
				if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			},
			complete: function(response) {
				//var returnedResponse = JSON.parse(response.responseText);
				console.log(response.responseText);
				console.log('sent config JSON with headers:', autoexec);
				console.log('now triggering reboot');
				$.ajax({
					url: '/reboot_ota.json',
					dataType: 'text',
					method: 'POST',
					cache: false,
					contentType: 'application/json; charset=utf-8',
					data: JSON.stringify({
						'timestamp': Date.now()
					}),
					error: function(xhr, ajaxOptions, thrownError) {
						console.log(xhr.status);
						console.log(thrownError);
						if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
					},
					complete: function(response) {
						console.log('reboot call completed');
					}
				});
			}
		});
	});

	$("input#save-autoexec1").on("click", function() {
		var data = {
			'timestamp': Date.now()
		};
		autoexec1 = $("#autoexec1").val();
		data['config'] = {};
		data['config'] = {
			autoexec1: {
				value: autoexec1,
				type: 33
			}
		}

		$.ajax({
			url: '/config.json',
			dataType: 'text',
			method: 'POST',
			cache: false,
			//            headers: { "X-Custom-autoexec1": autoexec1 },
			contentType: 'application/json; charset=utf-8',
			data: JSON.stringify(data),
			error: function(xhr, ajaxOptions, thrownError) {
				console.log(xhr.status);
				console.log(thrownError);
				if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			}
		});
		console.log('sent config JSON with headers:', autoexec1);
		console.log('sent data:', JSON.stringify(data));
	});

	$("input#save-gpio").on("click", function() {
		var data = {
			'timestamp': Date.now()
		};
		var config = {};

		var headers = {};
		$("input.gpio").each(function() {
			var id = $(this)[0].id;
			var pin = $(this).val();
			if (pin != '') {
				config[id] = {};
				config[id].value = pin;
				config[id].type = nvs_type_t.NVS_TYPE_STR;
			}
		});
		data['config'] = config;
		$.ajax({
			url: '/config.json',
			dataType: 'text',
			method: 'POST',
			cache: false,
			headers: headers,
			contentType: 'application/json; charset=utf-8',
			data: JSON.stringify(data),
			error: function(xhr, ajaxOptions, thrownError) {
				console.log(xhr.status);
				console.log(thrownError);
				if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			}
		});
		console.log('sent config JSON with headers:', JSON.stringify(headers));
		console.log('sent config JSON with data:', JSON.stringify(data));
	});

	$("#save-as-nvs").on("click", function() {
		var data = {
			'timestamp': Date.now()
		};
		var config = getConfigJson(true);
		const a = document.createElement("a");
		a.href = URL.createObjectURL(
			new Blob([JSON.stringify(config, null, 2)], {
				type: "text/plain"
			}));
		a.setAttribute("download", "nvs_config" + Date.now() + "json");
		document.body.appendChild(a);
		a.click();
		document.body.removeChild(a);
		console.log('sent config JSON with headers:', JSON.stringify(headers));
		console.log('sent config JSON with data:', JSON.stringify(data));
	});

	$("#save-nvs").on("click", function() {
		var headers = {};
		var data = {
			'timestamp': Date.now()
		};
		var config = getConfigJson(false);
		data['config'] = config;
		$.ajax({
			url: '/config.json',
			dataType: 'text',
			method: 'POST',
			cache: false,
			headers: headers,
			contentType: 'application/json; charset=utf-8',
			data: JSON.stringify(data),
			error: function(xhr, ajaxOptions, thrownError) {
				console.log(xhr.status);
				console.log(thrownError);
				if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			}
		});
		console.log('sent config JSON with headers:', JSON.stringify(headers));
		console.log('sent config JSON with data:', JSON.stringify(data));
	});
	$("#fwUpload").on("click", function() {
		var upload_path = "/flash.json";
		var fileInput = document.getElementById("flashfilename").files;
		if (fileInput.length == 0) {
			alert("No file selected!");
		} else {
			var file = fileInput[0];
			var xhttp = new XMLHttpRequest();
			xhttp.onreadystatechange = function() {
				if (xhttp.readyState == 4) {
					if (xhttp.status == 200) {
						showMessage(xhttp.responseText, 'MESSAGING_INFO')
					} else if (xhttp.status == 0) {
						showMessage("Upload connection was closed abruptly!", 'MESSAGING_ERROR');
					} else {
						showMessage(xhttp.status + " Error!\n" + xhttp.responseText, 'MESSAGING_ERROR');
					}
				}
			};
			xhttp.open("POST", upload_path, true);
			xhttp.send(file);
		}
		enableStatusTimer = true;
	});
	$("#flash").on("click", function() {
		var data = {
			'timestamp': Date.now()
		};
		if (blockFlashButton) return;
		blockFlashButton = true;
		var url = $("#fwurl").val();
		data['config'] = {
			fwurl: {
				value: url,
				type: 33
			}
		};

		$.ajax({
			url: '/config.json',
			dataType: 'text',
			method: 'POST',
			cache: false,
			contentType: 'application/json; charset=utf-8',
			data: JSON.stringify(data),
			error: function(xhr, ajaxOptions, thrownError) {
				console.log(xhr.status);
				console.log(thrownError);
				if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			}
		});
		enableStatusTimer = true;
	});

	$("#generate-command").on("click", function() {
		var commandLine = commandHeader + ' -n "' + $("#player").val() + '"';

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

	$('[name=audio]').on("click", function() {
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

	$('#fwcheck').on("click", function() {
		$("#releaseTable").html("");
		$.getJSON(releaseURL, function(data) {
				var i = 0;
				var branches = [];
				data.forEach(function(release) {
					namecomponents=release.name.split('#');
					ver=namecomponents[0];
					idf=namecomponents[1];
					cfg=namecomponents[2];
					branch=namecomponents[3];
					if (!branches.includes(branch)) {
						branches.push(branch);
					}
				});
				var fwb;
				branches.forEach(function(branch) {
					fwb += '<option value="' + branch + '">' + branch + '</option>';
				});
				$("#fwbranch").append(fwb);

				data.forEach(function(release) {
					var url = '';
					release.assets.forEach(function(asset) {
						if (asset.name.match(/\.bin$/)) {
							url = asset.browser_download_url;
						}
					});
					namecomponents = release.name.split('#');
					ver=namecomponents[0];
					idf=namecomponents[1];
					cfg=namecomponents[2];
					branch=namecomponents[3];
					
					var body = release.body;
					body = body.replace(/\'/ig, "\"");
					body = body.replace(/[\s\S]+(### Revision Log[\s\S]+)### ESP-IDF Version Used[\s\S]+/, "$1");
					body = body.replace(/- \(.+?\) /g, "- ");
					var [date, time] = release.created_at.split('T');
					var trclass = (i++ > 6) ? ' hide' : '';
					$("#releaseTable").append(
						"<tr class='release" + trclass + "'>" +
						"<td data-toggle='tooltip' title='" + body + "'>" + ver + "</td>" +
						"<td>" + date + "</td>" +
						"<td>" + cfg + "</td>" +
						"<td>" + idf + "</td>" +
						"<td>" + branch + "</td>" +
						"<td><input id='generate-command' type='button' class='btn btn-success' value='Select' data-url='" + url + "' onclick='setURL(this);' /></td>" +
						"</tr>"
					);
				});
				if (i > 7) {
					$("#releaseTable").append(
						"<tr id='showall'>" +
						"<td colspan='6'>" +
						"<input type='button' id='showallbutton' class='btn btn-info' value='Show older releases' />" +
						"</td>" +
						"</tr>"
					);
					$('#showallbutton').on("click", function() {
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

	$('input#searchinput').on("input", function() {
		var s = $('input#searchinput').val();
		var re = new RegExp(s, "gi");
		if (s.length == 0) {
			$("tr.release").removeClass("hide");
		} else if (s.length < 3) {
			$("tr.release").addClass("hide");
		} else {
			$("tr.release").addClass("hide");
			$("tr.release").each(function(tr) {
				$(this).find('td').each(function() {
					if ($(this).html().match(re)) {
						$(this).parent().removeClass('hide');
					}
				});
			});
		}
	});

	$("#fwbranch").change(function(e) {
		var branch = this.value;
		var re = new RegExp('^' + branch + '$', "gi");
		$("tr.release").addClass("hide");
		$("tr.release").each(function(tr) {
			$(this).find('td').each(function() {
				console.log($(this).html());
				if ($(this).html().match(re)) {
					$(this).parent().removeClass('hide');
				}
			});
		});
	});

	$('#boot-button').on("click", function() {
		enableStatusTimer = true;
	});
	$('#reboot-button').on("click", function() {
		enableStatusTimer = true;
	});

	$('#updateAP').on("click", function() {
		refreshAP(true);
		console.log("refresh AP");
	});

	//first time the page loads: attempt to get the connection status and start the wifi scan
	refreshAP(false);
	getConfig();
	getCommands();

	//start timers
	startCheckStatusInterval();
	//startRefreshAPInterval();

	$('[data-toggle="tooltip"]').tooltip({
		html: true,
		placement: 'right',
	});
	$('a[href^="#tab-firmware"]').dblclick(function() {
		dblclickCounter++;
		if (dblclickCounter >= 2) {
			dblclickCounter = 0;
			blockFlashButton = false;
			alert("Unocking flash button!");
		}
	});
	$('a[href^="#tab-firmware"]').click(function() {
		// when navigating back to this table, reset the counter
		if (!this.classList.contains("active")) dblclickCounter = 0;
	});
});

function setURL(button) {
	var url = button.dataset.url;
	$("#fwurl").val(url);

	$('[data-url^="http"]').addClass("btn-success").removeClass("btn-danger");
	$('[data-url="' + url + '"]').addClass("btn-danger").removeClass("btn-success");
}

function performConnect(conntype) {
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
		selectedSSID = $('#manual_ssid').val();
		pwd = $("#manual_pwd").val();
		dhcpname = $("#dhcp-name2").val();;
	} else {
		pwd = $("#pwd").val();
		dhcpname = $("#dhcp-name1").val();;
	}
	//reset connection 
	$("#loading").show();
	$("#connect-success").hide();
	$("#connect-fail").hide();

	$("#ok-connect").prop("disabled", true);
	$("#ssid-wait").text(selectedSSID);
	$("#connect").slideUp("fast", function() {});
	$("#connect_manual").slideUp("fast", function() {});
	$("#connect-wait").slideDown("fast", function() {});

	$.ajax({
		url: '/connect.json',
		dataType: 'text',
		method: 'POST',
		cache: false,
		//        headers: { 'X-Custom-ssid': selectedSSID, 'X-Custom-pwd': pwd, 'X-Custom-host_name': dhcpname },
		contentType: 'application/json; charset=utf-8',
		data: JSON.stringify({
			'timestamp': Date.now(),
			'ssid': selectedSSID,
			'pwd': pwd,
			'host_name': dhcpname
		}),
		error: function(xhr, ajaxOptions, thrownError) {
			console.log(xhr.status);
			console.log(thrownError);
			if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
		}
	});

	//now we can re-set the intervals regardless of result
	startCheckStatusInterval();
	startRefreshAPInterval();
}

function rssiToIcon(rssi) {
	if (rssi >= -60) {
		return 'w0';
	} else if (rssi >= -67) {
		return 'w1';
	} else if (rssi >= -75) {
		return 'w2';
	} else {
		return 'w3';
	}
}

function refreshAP(force) {
	if (!enableAPTimer && !force) return;
	$.getJSON("/scan.json", async function(data) {
		await sleep(2000);
		$.getJSON("/ap.json", function(data) {
			if (data.length > 0) {
				//sort by signal strength
				data.sort(function(a, b) {
					var x = a["rssi"];
					var y = b["rssi"];
					return ((x < y) ? 1 : ((x > y) ? -1 : 0));
				});
				apList = data;
				refreshAPHTML(apList);
			}
		});
	});
}

function refreshAPHTML(data) {
	var h = "";
	data.forEach(function(e, idx, array) {
		h += '<div class="ape{0}"><div class="{1}"><div class="{2}">{3}</div></div></div>'.format(idx === array.length - 1 ? '' : ' brdb', rssiToIcon(e.rssi), e.auth == 0 ? '' : 'pw', e.ssid);
		h += "\n";
	});

	$("#wifi-list").html(h)
}

function getMessages() {
	$.getJSON("/messages.json?1", async function(data) {
			for (const msg of data) {
				var msg_age = msg["current_time"] - msg["sent_time"];
				var msg_time = new Date();
				msg_time.setTime(msg_time.getTime() - msg_age);
				switch (msg["class"]) {
					case "MESSAGING_CLASS_OTA":
						//message: "{"ota_dsc":"Erasing flash complete","ota_pct":0}"
						var ota_data = JSON.parse(msg["message"]);
						if (ota_data.hasOwnProperty('ota_pct') && ota_data['ota_pct'] != 0) {
							otapct = ota_data['ota_pct'];
							$('.progress-bar').css('width', otapct + '%').attr('aria-valuenow', otapct);
							$('.progress-bar').html(otapct + '%');
						}
						if (ota_data.hasOwnProperty('ota_dsc') && ota_data['ota_dsc'] != '') {
							otadsc = ota_data['ota_dsc'];
							$("span#flash-status").html(otadsc);
							if (msg.type == "MESSAGING_ERROR" || otapct > 95) {
								blockFlashButton = false;
								enableStatusTimer = true;
							}
						}
						break;
					case "MESSAGING_CLASS_STATS":
						// for task states, check structure : task_state_t
						var stats_data = JSON.parse(msg["message"]);
						console.log(msg_time.toLocaleString() + " - Number of tasks on the ESP32: " + stats_data["ntasks"]);
						var stats_tasks = stats_data["tasks"];
						console.log(msg_time.toLocaleString() + '\tname' + '\tcpu' + '\tstate' + '\tminstk' + '\tbprio' + '\tcprio' + '\tnum');
						stats_tasks.forEach(function(task) {
							console.log(msg_time.toLocaleString() + '\t' + task["nme"] + '\t' + task["cpu"] + '\t' + task_state_t[task["st"]] + '\t' + task["minstk"] + '\t' + task["bprio"] + '\t' + task["cprio"] + '\t' + task["num"]);
						});
						break;
					case "MESSAGING_CLASS_SYSTEM":
						var r = await showMessage(msg["message"], msg["type"], msg_age);

						$("#syslogTable").append(
							"<tr class='" + msg["type"] + "'>" +
							"<td>" + msg_time.toLocaleString() + "</td>" +
							"<td>" + escapeHTML(msg["message"]).replace(/\n/g, '<br />') + "</td>" +
							"</tr>"
						);
						break;
					default:
						break;
				}
			}
		})
		.fail(function(xhr, ajaxOptions, thrownError) {
			console.log(xhr.status);
			console.log(thrownError);
			if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
		});
	/*
    Minstk is minimum stack space left
Bprio is base priority
cprio is current priority
nme is name
st is task state. I provided a "typedef" that you can use to convert to text
cpu is cpu percent used
*/
}

function checkStatus() {
	RepeatCheckStatusInterval();
	if (!enableStatusTimer) return;
	if (blockAjax) return;
	blockAjax = true;
	getMessages();
	$.getJSON("/status.json", function(data) {
			if (data.hasOwnProperty('ssid') && data['ssid'] != "") {
				if (data["ssid"] === selectedSSID) {
					//that's a connection attempt
					if (data["urc"] === 0) {
						//got connection
						$("#connected-to span").text(data["ssid"]);
						$("#connect-details h1").text(data["ssid"]);
						$("#ip").text(data["ip"]);
						$("#netmask").text(data["netmask"]);
						$("#gw").text(data["gw"]);
						$("#wifi-status").slideDown("fast", function() {});
						$("span#foot-wifi").html(", SSID: <strong>" + data["ssid"] + "</strong>, IP: <strong>" + data["ip"] + "</strong>");

						//unlock the wait screen if needed
						$("#ok-connect").prop("disabled", false);

						//update wait screen
						$("#loading").hide();
						$("#connect-success").text("Your IP address now is: " + data["ip"]);
						$("#connect-success").show();
						$("#connect-fail").hide();

						enableAPTimer = false;
						if (!recovery) enableStatusTimer = false;
					} else if (data["urc"] === 1) {
						//failed attempt
						$("#connected-to span").text('');
						$("#connect-details h1").text('');
						$("#ip").text('0.0.0.0');
						$("#netmask").text('0.0.0.0');
						$("#gw").text('0.0.0.0');
						$("span#foot-wifi").html("");

						//don't show any connection
						$("#wifi-status").slideUp("fast", function() {});

						//unlock the wait screen
						$("#ok-connect").prop("disabled", false);

						//update wait screen
						$("#loading").hide();
						$("#connect-fail").show();
						$("#connect-success").hide();

						enableAPTimer = true;
						enableStatusTimer = true;
					}
				} else if (data.hasOwnProperty('urc') && data['urc'] === 0) {
					//ESP32 is already connected to a wifi without having the user do anything
					if (!($("#wifi-status").is(":visible"))) {
						$("#connected-to span").text(data["ssid"]);
						$("#connect-details h1").text(data["ssid"]);
						$("#ip").text(data["ip"]);
						$("#netmask").text(data["netmask"]);
						$("#gw").text(data["gw"]);
						$("#wifi-status").slideDown("fast", function() {});
						$("span#foot-wifi").html(", SSID: <strong>" + data["ssid"] + "</strong>, IP: <strong>" + data["ip"] + "</strong>");
					}
					enableAPTimer = false;
					if (!recovery) enableStatusTimer = false;
				}
			} else if (data.hasOwnProperty('urc') && data['urc'] === 2) {
				//that's a manual disconnect
				if ($("#wifi-status").is(":visible")) {
					$("#wifi-status").slideUp("fast", function() {});
					$("span#foot-wifi").html("");
				}
				enableAPTimer = true;
				enableStatusTimer = true;
			}
			if (data.hasOwnProperty('recovery')) {
				if (LastRecoveryState != data["recovery"]) {
					LastRecoveryState = data["recovery"];
					$("input#show-nvs")[0].checked = LastRecoveryState == 1 ? true : false;
				}
				if ($("input#show-nvs")[0].checked) {
					$('a[href^="#tab-nvs"]').show();
				} else {
					$('a[href^="#tab-nvs"]').hide();
				}

				if (data["recovery"] === 1) {
					recovery = true;
					$("#otadiv").show();
					$('a[href^="#tab-audio"]').hide();
					$('a[href^="#tab-gpio"]').show();
					$('#uploaddiv').show();
					$("footer.footer").removeClass('sl');
					$("footer.footer").addClass('recovery');
					$("#boot-button").html('Reboot');
					$("#boot-form").attr('action', '/reboot_ota.json');

					enableStatusTimer = true;
				} else {
					recovery = false;
					$("#otadiv").hide();
					$('a[href^="#tab-audio"]').show();
					$('a[href^="#tab-gpio"]').hide();
					$('#uploaddiv').hide();
					$("footer.footer").removeClass('recovery');
					$("footer.footer").addClass('sl');
					$("#boot-button").html('Recovery');
					$("#boot-form").attr('action', '/recovery.json');

					enableStatusTimer = false;
				}
			}
			if (data.hasOwnProperty('project_name') && data['project_name'] != '') {
				pname = data['project_name'];
			}
			if (data.hasOwnProperty('version') && data['version'] != '') {
				ver = data['version'];
				$("span#foot-fw").html("fw: <strong>" + ver + "</strong>, mode: <strong>" + pname + "</strong>");
			} else {
				$("span#flash-status").html('');
			}

			if (data.hasOwnProperty('Voltage')) {
				var voltage = data['Voltage'];
				var layer;

				/* Assuming Li-ion 18650s as a power source, 3.9V per cell, or above is treated
					as full charge (>75% of capacity).  3.4V is empty. The gauge is loosely
					following the graph here:
						https://learn.adafruit.com/li-ion-and-lipoly-batteries/voltages
					using the 0.2C discharge profile for the rest of the values.
				*/

				if (voltage > 0) {
					if (inRange(voltage, 5.8, 6.8) || inRange(voltage, 8.8, 10.2)) {
						layer = bat0;
					} else if (inRange(voltage, 6.8, 7.4) || inRange(voltage, 10.2, 11.1)) {
						layer = bat1;
					} else if (inRange(voltage, 7.4, 7.5) || inRange(voltage, 11.1, 11.25)) {
						layer = bat2;
					} else if (inRange(voltage, 7.5, 7.8) || inRange(voltage, 11.25, 11.7)) {
						layer = bat3;	
					} else {
						layer = bat4;
					}
					layer.setAttribute("display","inline");
				}
			}
			if (data.hasOwnProperty('Jack')) {
				var jack = data['Jack'];
				if (jack == '1') {
					o_jack.setAttribute("display", "inline");
				}
			}
			blockAjax = false;
		})
		.fail(function(xhr, ajaxOptions, thrownError) {
			console.log(xhr.status);
			console.log(thrownError);
			if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			blockAjax = false;
		});
}

function runCommand(button, reboot) {
	cmdstring = button.attributes.cmdname.value;
	fields = document.getElementById("flds-" + cmdstring);
	cmdstring += ' ';
	if (fields) {
		allfields = fields.querySelectorAll("select,input");
		for (i = 0; i < allfields.length; i++) {
			attr = allfields[i].attributes;
			qts = '';
			opt = '';
			optspacer = ' ';
			isSelect = allfields[i].attributes?.class?.value == "custom-select";
			if ((isSelect && allfields[i].selectedIndex != 0) || !isSelect) {
				if (attr.longopts.value !== "undefined") {
					opt += '--' + attr.longopts.value;
					optspacer = '=';
				} else if (attr.shortopts.value !== "undefined") {
					opt = '-' + attr.shortopts.value;
				}

				if (attr.hasvalue.value == "true") {
					if (allfields[i].value != '') {
						qts = (/\s/.test(allfields[i].value)) ? '"' : '';
						cmdstring += opt + optspacer + qts + allfields[i].value + qts + ' ';
					}
				} else {
					// this is a checkbox
					if (allfields[i].checked) cmdstring += opt + ' ';
				}
			}
		}
	}
	console.log(cmdstring);

	var data = {
		'timestamp': Date.now()
	};
	data['command'] = cmdstring;

	$.ajax({
		url: '/commands.json',
		dataType: 'text',
		method: 'POST',
		cache: false,
		contentType: 'application/json; charset=utf-8',
		data: JSON.stringify(data),
		error: function(xhr, ajaxOptions, thrownError) {
			console.log(xhr.status);
			console.log(thrownError);
			if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');

		},
		complete: function(response) {
			//var returnedResponse = JSON.parse(response.responseText);
			console.log(response.responseText);
			if (reboot) {
				showMessage('Applying. Please wait for the ESP32 to reboot', 'MESSAGING_WARNING');
				console.log('now triggering reboot');
				$.ajax({
					url: '/reboot.json',
					dataType: 'text',
					method: 'POST',
					cache: false,
					contentType: 'application/json; charset=utf-8',
					data: JSON.stringify({
						'timestamp': Date.now()
					}),
					error: function(xhr, ajaxOptions, thrownError) {
						console.log(xhr.status);
						console.log(thrownError);
						if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
					},
					complete: function(response) {
						console.log('reboot call completed');
						Promise.resolve().delay(5000).then(function(v) {
							console.log('Getting updated commands');
							getCommands();
						});
					}
				});
			}
		}
	});
	enableStatusTimer = true;
}

function getCommands() {
	$.getJSON("/commands.json", function(data) {
			console.log(data);
			data.commands.forEach(function(command) {
				if ($("#flds-" + command.name).length == 0) {
					isConfig = ($('#' + command.name + '-list').length > 0);
					innerhtml = '';
					//innerhtml+='<tr class="table-light"><td>'+(isConfig?'<h1>':'');
					innerhtml += '<div class="card text-white bg-primary mb-3"><div class="card-header">' + escapeHTML(command.help).replace(/\n/g, '<br />') + '</div><div class="card-body">';
					innerhtml += '<fieldset id="flds-' + command.name + '">';
					if (command.hasOwnProperty("argtable")) {
						command.argtable.forEach(function(arg) {
							placeholder = arg?.datatype || '';
							ctrlname = command.name + '-' + arg.longopts;
							curvalue = data.values?. [command.name]?. [arg.longopts];

							var attributes = 'hasvalue=' + arg.hasvalue + ' ';
							//attributes +='datatype="'+arg.datatype+'" ';
							attributes += 'longopts="' + arg.longopts + '" ';
							attributes += 'shortopts="' + arg.shortopts + '" ';
							attributes += 'checkbox=' + arg.checkbox + ' ';
							attributes += 'cmdname="' + command.name + '" ';
							attributes += 'id="' + ctrlname + '" name="' + ctrlname + '" hasvalue="' + arg.hasvalue + '"   ';
							if (arg.checkbox) {
								innerhtml += '<div class="form-check"><label class="form-check-label">';
								innerhtml += '<input type="checkbox" ' + attributes + ' class="form-check-input" value="" >' + arg.glossary + '<small class="form-text text-muted">Previous value: ' + (curvalue?"Checked":"Unchecked") + '</small></label>';
							} else {
								innerhtml += '<div class="form-group" ><label for="' + ctrlname + '">' + arg.glossary + '</label>';
								if (placeholder.includes('|')) {
									placeholder = placeholder.replace('<', '').replace('>', '');
									innerhtml += '<select ' + attributes + ' class="form-control"';
									placeholder = '--|' + placeholder;
									placeholder.split('|').forEach(function(choice) {
										innerhtml += '<option >' + choice + '</option>';
									});
									innerhtml += '</select>';
								} else {
									innerhtml += '<input type="text" class="form-control" placeholder="' + placeholder + '" ' + attributes + '>';
								}
								innerhtml += '<small class="form-text text-muted">Previous value: ' + (curvalue || '') + '</small>';
							}
							innerhtml += '</div>';
						});
					}
					innerhtml +='<div style="margin-top: 16px;">';
					if (isConfig) {
						innerhtml += '<button type="submit" class="btn btn-info" id="btn-' + command.name + '" cmdname="' + command.name + '" onclick="runCommand(this,false)">Save</button>';
						innerhtml += '<button type="submit" class="btn btn-warning" id="btn-' + command.name + '" cmdname="' + command.name + '" onclick="runCommand(this,true)">Apply</button>';
					} else {
						innerhtml += '<button type="submit" class="btn btn-success" id="btn-' + command.name + '" cmdname="' + command.name + '" onclick="runCommand(this,false)">Execute</button>';
					}
					innerhtml += '</div></fieldset></div></div>';

					if (isConfig) {
						$('#' + command.name + '-list').append(innerhtml);
					} else {
						$("#commands-list").append(innerhtml);
					}
				}
			});

			data.commands.forEach(function(command) {
				if (command.hasOwnProperty("argtable")) {
					command.argtable.forEach(function(arg) {
						ctrlselector = '#' + command.name + '-' + arg.longopts;
						if (arg.checkbox) {
							$(ctrlselector)[0].checked = data.values?. [command.name]?. [arg.longopts];
						} else {
							$(ctrlselector)[0].value = data.values?. [command.name]?. [arg.longopts] || '';
							if ($(ctrlselector)[0].value.length == 0 && (arg?.datatype || '').includes('|')) {
								$(ctrlselector)[0].value = '--';
							}

						}

					});
				}
			});

		})
		.fail(function(xhr, ajaxOptions, thrownError) {
			console.log(xhr.status);
			console.log(thrownError);
			$("#commands-list").empty();
			if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			blockAjax = false;
		});
}

function getConfig() {
	$.getJSON("/config.json", function(data) {
			Object.keys(data.hasOwnProperty('config') ? data.config : data).sort().forEach(function(key, i) {
				if (data.hasOwnProperty(key)) {
					if (key == 'autoexec') {
						if (data["autoexec"].value === "1") {
							$("#autoexec-cb")[0].checked = true;
						} else {
							$("#autoexec-cb")[0].checked = false;
						}
					} else if (key == 'autoexec1') {
						$("textarea#autoexec1").val(data[key].value);
						var re = / -o "?(\S+)\b/g;
						var m = re.exec(data[key].value);
						if (m[1] == 'I2S') {
							o_i2s.setAttribute("display", "inline");
						} else if (m[1] == 'SPDIF') {
							o_spdif.setAttribute("display", "inline");
						} else if (m[1] == 'BT') {
							o_bt.setAttribute("display", "inline");
						}
					} else if (key == 'host_name') {
						$("input#dhcp-name1").val(data[key].value);
						$("input#dhcp-name2").val(data[key].value);
					}

					$("tbody#nvsTable").append(
						"<tr>" +
						"<td>" + key + "</td>" +
						"<td class='value'>" +
						"<input type='text' class='form-control nvs' id='" + key + "'  nvs_type=" + data[key].type + " >" +
						"</td>" +
						"</tr>"
					);
					$("input#" + key).val(data[key].value);
				}
			});
			$("tbody#nvsTable").append("<tr><td><input type='text' class='form-control' id='nvs-new-key' placeholder='new key'></td><td><input type='text' class='form-control' id='nvs-new-value' placeholder='new value' nvs_type=33 ></td></tr>");
			if (data.hasOwnProperty('gpio')) {
				data.gpio.forEach(function(gpio_entry) {
					cl = gpio_entry.fixed ? "table-light" : "table-dark";
					$("tbody#gpiotable").append('<tr class=' + cl + '><th scope="row">' + gpio_entry.group + '</th><td>' + gpio_entry.name + '</td><td>' + gpio_entry.gpio + '</td><td>' + (gpio_entry.fixed ? 'Fixed':'Configuration') + '</td></tr>');
				});
			}
		})
		.fail(function(xhr, ajaxOptions, thrownError) {
			console.log(xhr.status);
			console.log(thrownError);
			if (thrownError != '') showMessage(thrownError, 'MESSAGING_ERROR');
			blockAjax = false;
		});
}

function showMessage(message, severity, age = 0) {
	if (severity == 'MESSAGING_INFO') {
		$('#message').css('background', '#6af');
	} else if (severity == 'MESSAGING_WARNING') {
		$('#message').css('background', '#ff0');
	} else if (severity == 'MESSAGING_ERROR') {
		$('#message').css('background', '#f00');
	} else {
		$('#message').css('background', '#f00');
	}

	$('#message').html(message);
	return new Promise(function(resolve, reject) {
		$("#content").fadeTo("slow", 0.3, function() {
			$("#message").show(500).delay(5000).hide(500, function() {
				$("#content").fadeTo("slow", 1.0, function() {
					resolve(true);
				});
			});
		});
	});

}

function inRange(x, min, max) {
	return ((x - min) * (x - max) <= 0);
}

function sleep(ms) {
	return new Promise(resolve => setTimeout(resolve, ms));
}