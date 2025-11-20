var currentpath = "/";
var authentication = false;
var websocket_port = 0;
var websocket_IP = "";
var async_webcommunication = false;
var page_id = "";
var ws_source;
var log_off =false;
var websocket_started =false;
var esp_error_message ="";
var esp_error_code = 0;
var xmlhttpupload;
var typeupload = 0;
function navbar(){
    var content="<table><tr>";
    var tlist = currentpath.split("/");
    var path="/";
    var nb = 1;
    content+="<td class='btnimg'  onclick=\"currentpath='/'; SendCommand('list','all');\">/</td>";
    while (nb < (tlist.length-1))
        {
            path+=tlist[nb] + "/";
            content+="<td class='btnimg' onclick=\"currentpath='"+path+"'; SendCommand('list','all');\">"+tlist[nb] +"</td><td>/</td>";
            nb++;
        }
        content+="</tr></table>";
    return content;
}

function trash_icon(){
    var content ="<svg width='24' height='24' viewBox='0 0 128 128'>";
    content +="<rect x='52' y='12' rx='6' ry='6' width='25' height='7' style='fill:red;' />";
    content +="<rect x='52' y='16' width='25' height='2' style='fill:white;' />";
    content +="<rect x='30' y='18' rx='6' ry='6' width='67' height='100' style='fill:red;' />";
    content +="<rect x='20' y='18' rx='10' ry='10' width='87' height='14' style='fill:red;' />";
    content +="<rect x='20' y='29' width='87' height='3' style='fill:white;' />";
    content +="<rect x='40' y='43' rx='7' ry='7' width='7' height='63' style='fill:white;' />";
    content +="<rect x='60' y='43' rx='7' ry='7' width='7' height='63' style='fill:white;' />";
    content +="<rect x='80' y='43' rx='7' ry='7' width='7' height='63' style='fill:white;' /></svg>";
    return content;
}

function back_icon(){
  var content ="<svg width='24' height='24' viewBox='0 0 24 24'><path d='M7,3 L2,8 L7,13 L7,10 L17,10 L18,11 L18,15 L17,16 L10,16 L9,17 L9,19 L10,20 L20,20 L22,18 L22,8 L20,6 L7,6 z' stroke='black' fill='white' /></svg>";
  return content;
}

function select_dir(directoryname){
    currentpath+=directoryname + "/";
    SendCommand('list','all');
}

function compareStrings(a, b) {
  // case-insensitive comparison
  a = a.toLowerCase();
  b = b.toLowerCase();
  return (a < b) ? -1 : (a > b) ? 1 : 0;
}

function dispatchfilestatus(jsonresponse)
{
var content ="";
var display_message = false;
content ="&nbsp;&nbsp;Status: "+jsonresponse.status;
content +="&nbsp;&nbsp;|&nbsp;&nbsp;Total space: "+jsonresponse.total;
content +="&nbsp;&nbsp;|&nbsp;&nbsp;Used space: "+jsonresponse.used;
content +="&nbsp;&nbsp;|&nbsp;&nbsp;Occupation: ";
content +="<meter min='0' max='100' high='90' value='"+jsonresponse.occupation +"'></meter>&nbsp;"+jsonresponse.occupation +"%";
document.getElementById('status').innerHTML=content;
content ="";
if (currentpath!="/")
    {
     var pos = currentpath.lastIndexOf("/",currentpath.length-2);
     var previouspath = currentpath.slice(0,pos+1);
     content +="<tr style='cursor:hand;' onclick=\"currentpath='"+previouspath+"'; SendCommand('list','all');\"><td >"+back_icon()+"</td><td colspan='4'> Up..</td></tr>";
    }
jsonresponse.files.sort(function(a, b) {
    return compareStrings(a.name, b.name);
});
if (currentpath=="/") {
    display_message = true;
}
var display_time =false;
for (var i1=0;i1 <jsonresponse.files.length;i1++){
//first display files
if (String(jsonresponse.files[i1].size) != "-1")
    {
    content +="<TR>";
    content +="<td><svg height='24' width='24' viewBox='0 0 24 24' >    <path d='M1,2 L1,21 L2,22 L16,22 L17,21 L17,6 L12,6 L12,1  L2,1 z' stroke='black' fill='white' /><line x1='12' y1='1' x2='17' y2='6' stroke='black' stroke-width='1'/>";
    content +="</svg></td>";
    content +="<TD class='btnimg' style=\"padding:0px;\"><a href=\""+jsonresponse.path+jsonresponse.files[i1].name+"\" target=_blank><div class=\"blacklink\">";
    content +=jsonresponse.files[i1].name;
    if ((jsonresponse.files[i1].name == "index.html.gz")||(jsonresponse.files[i1].name == "index.html")){
        display_message = false;
    }
    content +="</div></a></TD><TD>";
    content +=jsonresponse.files[i1].size;
    content +="</TD>";
    if (jsonresponse.files[i1].hasOwnProperty('time')){
        display_time = true;
        content +="<TD>";
        content += jsonresponse.files[i1].time;
        content +="</TD>";
    } else {
    content +="<TD></TD>";    
    }
    content +="<TD width='0%'><div class=\"btnimg\" onclick=\"Delete('"+jsonresponse.files[i1].name+"')\">";
    content +=trash_icon();
    content +="</div></TD><td></td></TR>";
    }
}
//then display directories
for (var i2=0;i2 <jsonresponse.files.length;i2++){
if (String(jsonresponse.files[i2].size) == "-1")
    {
    content+="<TR><td><svg height='24' width='24' viewBox='0 0 24 24' ><path d='M19,11 L19,8 L18,7 L8,7 L8,5 L7,4 L2,4 L1,5 L1,22 L19,22 L20,21 L23,11 L5,11 L2,21 L1,22' stroke='black' fill='white' /></svg></td>";
    content +="<TD  class='btnimg blacklink' style='padding:10px 15px;' onclick=\"select_dir('" + jsonresponse.files[i2].name+"');\">";
    content +=jsonresponse.files[i2].name;
    content +="</TD><TD></TD><TD></TD>";
    if (typeof jsonresponse.files[i2].hasOwnProperty('time')){
        display_time = true;
    }
    content +="<TD width='0%'><div class=\"btnimg\" onclick=\"Deletedir('"+jsonresponse.files[i2].name+"')\">";
    content +=trash_icon();
    content +="</div></TD><td></td></TR>";
    }
}
if(display_time){
    document.getElementById('FS_time').innerHTML = "";
} else {
    document.getElementById('FS_time').innerHTML = "Time";
}
 if (display_message) {
    
    document.getElementById('MSG').innerHTML = "File index.html.gz is missing, please upload it";
 } else {
     document.getElementById('MSG').innerHTML = "<a href='/' class= 'btn btn-primary'>Go to ESP3D interface</a>";
 }
 document.getElementById('file_list').innerHTML=content;
 document.getElementById('path').innerHTML=navbar();}

function Delete(filename){
if (confirm("Confirm deletion of file: " + filename))SendCommand("delete",filename);
}

function Deletedir(filename){
if (confirm("Confirm deletion of directory: " + filename))SendCommand("deletedir",filename);
}

function Createdir(){
var filename = prompt("Directory name", "");
if (filename != null) {
   SendCommand("createdir",filename.trim());
    }
}

function SendCommand(action,filename){
var xmlhttp = new XMLHttpRequest();
var url = "/files?action="+action;
document.getElementById('MSG').innerHTML = "Connecting...";
url += "&filename="+encodeURI(filename);
url += "&path="+encodeURI(currentpath);
xmlhttp.onreadystatechange = function() {
    if (xmlhttp.readyState == 4 ) {
        if(xmlhttp.status == 200) {
        var jsonresponse = JSON.parse(xmlhttp.responseText);
        dispatchfilestatus(jsonresponse);
        } else {
            if(xmlhttp.status == 401) { 
                RL ();
            } else {
                console.log(xmlhttp.status);
                FWError();
            }
        }
    }
};
xmlhttp.open("GET", url, true);
xmlhttp.send();
}

function Sendfile(){
var files = document.getElementById('file-select').files;
if (files.length==0)return;
document.getElementById('upload-button').value = "Uploading...";
document.getElementById('prg').style.visibility = "visible";
var formData = new FormData();
formData.append('path', currentpath);
for (var i3 = 0; i3 < files.length; i3++) {
var file = files[i3];
var arg = currentpath + file.name + "S";
 //append file size first to check upload is complete
 formData.append(arg, file.size);
 formData.append('myfiles[]', file, currentpath+file.name);}
xmlhttpupload = new XMLHttpRequest();
xmlhttpupload.open('POST', '/files', true);
//progress upload event
xmlhttpupload.upload.addEventListener("progress", updateProgress, false);
//progress function
function updateProgress (oEvent) {
  if (oEvent.lengthComputable) {
    var percentComplete = (oEvent.loaded / oEvent.total)*100;
    document.getElementById('prg').value=percentComplete;
    document.getElementById('upload-button').value = "Uploading ..." + percentComplete.toFixed(0)+"%" ;
  } else {
    // Impossible because size is unknown
  }
}
typeupload = 1;
xmlhttpupload.onload = function () {
 if (xmlhttpupload.status === 200) {
document.getElementById('upload-button').value = 'Upload';
document.getElementById('prg').style.visibility = "hidden";
document.getElementById('file-select').value="";
var jsonresponse = JSON.parse(xmlhttpupload.responseText);
dispatchfilestatus(jsonresponse);
 } else uploadError();
};

xmlhttpupload.send(formData);
}

function padNumber(num, size) {
    var s = num.toString();
    while (s.length < size) s = "0" + s;
        return s;
}
function getPCTime(){
    var d = new Date();
    return d.getFullYear() + "-" + padNumber(d.getMonth() + 1 ,2) + "-" + padNumber(d.getDate(),2) + "-" + padNumber(d.getHours(),2) + "-" + padNumber(d.getMinutes(),2) + "-" + padNumber(d.getSeconds(),2); 
}

function HideAll(msg){
    //console.log("Hide all:" + msg);
    log_off = true;
    if(websocket_started){
        ws_source.close();
    }
    document.title = document.title + "(disconnected)";
    document.getElementById('MSG').innerHTML = msg;
    document.getElementById('FILESYSTEM').style.display = "none";
    document.getElementById('FWUPDATE').style.display = "none";
}

function FWError(){
    HideAll("Failed to communicate with FW!");
}

function FWOk(){
    document.getElementById('MSG').innerHTML = "Connected";
    document.getElementById('FILESYSTEM').style.display = "block";
    document.getElementById('FWUPDATE').style.display = "block";
}

function InitUI(){
var xmlhttp = new XMLHttpRequest();
var url = "/command?commandText="+encodeURI("[ESP800]");
authentication = false;
async_webcommunication = false;
console.log("Init UI");
xmlhttp.onreadystatechange = function() {
    if (xmlhttp.readyState == 4 ) { 
        var error = false;
        if(xmlhttp.status == 200) {
        var response = xmlhttp.responseText;
        var nbitem = 0;
        var tresp = response.split("#");
        console.log(xmlhttp.responseText);
        if (tresp.length < 3) {
            error = true;
        } else {
            //FW version:1.1f # FW target:grbl-embedded  # FW HW:Direct SD  # primary sd:/sd # secondary sd:none # authentication:no # webcommunication: Sync: 81:192.168.0.1 # hostname:grblesp # axis:3
            for (var p=0; p < tresp.length; p++){
             var sublist = tresp[p].split(":");
             if (sublist[0].trim() == "FW version"){
                 document.getElementById('FWVERSION').innerHTML = "v"+sublist[1];
                 nbitem++;
             }
             if (sublist[0].trim() == "authentication"){
                 if (sublist[1].trim() == "no") {
                     authentication = false;
                     document.getElementById('loginicon').style.visibility = "hidden";
                 }
                 else {
                     authentication = true;
                     document.getElementById('loginicon').style.visibility = "visible";
                 }
                 nbitem++;
             }
             if (sublist[0].trim() == "webcommunication"){
                 websocket_port = sublist[2].trim();
                 if (sublist.length>3) {
                     websocket_IP = sublist[3].trim();
                 } else {
                     websocket_IP =  document.location.hostname;
                 }
                 startSocket();
                 nbitem++;
             }
             if (sublist[0].trim() == "hostname"){
                 document.title = sublist[1].trim();
                 nbitem++;
             }
            }
            if (nbitem == 4) {
                SendCommand('list','all');
                FWOk();
            } else {
                error = true;
                
            }
        }
            
               

        } else if (xmlhttp.status == 401){
            RL();
        } else {
            error = true;
            console.log( xmlhttp.status);
        }
        if (error) {
            FWError();
            }
    }
};
xmlhttp.open("GET", url, true);
xmlhttp.send();
}

function startSocket(){
      if (websocket_started){
          ws_source.close();
      }
      if(async_webcommunication){
        ws_source = new WebSocket('ws://'+websocket_IP + ':' + websocket_port +'/ws',['arduino']);
        }
      else {
          //console.log("Socket port is :" + websocket_port);
          ws_source = new WebSocket('ws://'+websocket_IP + ':' + websocket_port,['arduino']);
      }
      ws_source.binaryType = "arraybuffer";
      ws_source.onopen = function(e){
        console.log("WS");
        websocket_started = true;
      };
      ws_source.onclose = function(e){
        websocket_started = false;
        console.log("~WS");
        //seems sometimes it disconnect so wait 3s and reconnect
        //if it is not a log off
        if(!log_off) setTimeout(startSocket, 3000);
      };
      ws_source.onerror = function(e){
        console.log("WS", e);
      };
      ws_source.onmessage = function(e){
        var msg = "";
        //bin
        if(!(e.data instanceof ArrayBuffer)){
          msg = e.data;
          var tval = msg.split(":");
          if (tval.length >= 2) {
          if (tval[0] == 'currentID') {
              page_id = tval[1];
              console.log("ID " + page_id); 
          }
          if (tval[0] == 'activeID') {
              if(page_id != tval[1]) {
                HideAll("It seems you are connect from another location, your are now disconnected");
                }
            }
         if (tval[0] == 'ERROR') {
              esp_error_message = tval[2];
              esp_error_code = tval[1];
              console.log(tval[2] + " code:" +  tval[1]);
              uploadError();
              xmlhttpupload.abort();
            }
          }
          
        }
        //console.log(msg);
        
      };
}

window.onload = function() {
InitUI();
};

function uploadError()
{
    if (esp_error_code != 0) {
        alert('Update failed(' + esp_error_code + '): ' + esp_error_message);
        esp_error_code = 0;
    } else {
        alert('Update failed!');
    }
    
    if (typeupload == 1) {
        //location.reload();
        document.getElementById('upload-button').value = 'Upload';
        document.getElementById('prg').style.visibility = "hidden";
        document.getElementById('file-select').value="";
        SendCommand('list', 'all');
    } else {
        location.reload();
    }
}

function Uploadfile(){
if (!confirm("Confirm Firmware Update ?"))return;
var files = document.getElementById('fw-select').files;
if (files.length==0)return;
document.getElementById('ubut').style.visibility = 'hidden';
document.getElementById('fw-select').style.visibility = 'hidden';
document.getElementById('msg').style.visibility = "visible";
document.getElementById('msg').innerHTML="";
document.getElementById('FILESYSTEM').style.display = "none";
document.getElementById('prgfw').style.visibility = "visible";
var formData = new FormData();
for (var i4 = 0; i4 < files.length; i4++) {
var file = files[i4];
var arg =  "/" + file.name + "S";
 //append file size first to check upload is complete
 formData.append(arg, file.size);
 formData.append('myfile[]', file, "/"+file.name);}
typeupload = 0;
xmlhttpupload = new XMLHttpRequest();
xmlhttpupload.open('POST', '/updatefw', true);
//progress upload event
xmlhttpupload.addEventListener("progress", updateProgress, false);
//progress function
function updateProgress (oEvent) {
  if (oEvent.lengthComputable) {
    var percentComplete = (oEvent.loaded / oEvent.total)*100;
    document.getElementById('prgfw').value=percentComplete;
   document.getElementById('msg').innerHTML = "Uploading ..." + percentComplete.toFixed(0)+"%" ;
  } else {
    // Impossible because size is unknown
  }
}
xmlhttpupload.onload = function () {
 if (xmlhttpupload.status === 200) {
document.getElementById('ubut').value = 'Upload';
document.getElementById('msg').innerHTML="Restarting, please wait....";
document.getElementById('counter').style.visibility = "visible";
document.getElementById('ubut').style.visibility = 'hidden';
document.getElementById('ubut').style.width = '0px';
document.getElementById('fw-select').value="";
document.getElementById('fw-select').style.visibility = 'hidden';
document.getElementById('fw-select').style.width = '0px';

var jsonresponse = JSON.parse(xmlhttpupload.responseText);
if (jsonresponse.status=='1' || jsonresponse.status=='4' || jsonresponse.status=='1')uploadError();
if (jsonresponse.status=='2')alert('Update canceled!');
else if (jsonresponse.status=='3')
{
    var i5 = 0;
    var interval;
    var x = document.getElementById("prgfw");
    x.max=40;
    interval = setInterval(function(){
        i5=i5+1;
        var x = document.getElementById("prgfw");
        x.value=i5;
        document.getElementById('counter').innerHTML=41-i5;
        if (i5>40)
            {
            clearInterval(interval);
            location.reload();
            }
        },1000);
}
else uploadError();
 } else uploadError();
};
xmlhttpupload.send(formData);
}


function RL(){
    document.getElementById('loginpage').style.display='block';
}

function SLR (){
    document.getElementById('loginpage').style.display='none';
    var user = document.getElementById('lut').value.trim();
    var password = document.getElementById('lpt').value.trim();
    var url = "/login?USER="+encodeURIComponent(user) + "&PASSWORD=" + encodeURIComponent(password) + "&SUBMIT=yes" ;
    var xmlhttp = new XMLHttpRequest();
    xmlhttp.onreadystatechange = function() {
        if (xmlhttp.readyState == 4){
            if (xmlhttp.status != 200) {
                if (xmlhttp.status == 401) {
                    RL();
                } else {
                    FWError();
                    console.log(xmlhttp.status);
                }
            } else {
                InitUI();
            }
        }
    };
xmlhttp.open("GET", url, true);
xmlhttp.send();
}
