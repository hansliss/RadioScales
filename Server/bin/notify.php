<?php
$string = file_get_contents("/opt/saltScale/etc/config.json");
$conf = json_decode($string, true);
$string = file_get_contents("/opt/saltScale/data/state.json");
$state = json_decode($string, true);
if (strcmp($state["status"], "Normal")) {
   mail($conf["emailrcpt"],"Salt level is ".$state["status"],"The current total weight is " . $state["weight"] . "kg.");
   $state["notified"] = "yes";
   file_put_contents("/opt/saltScale/data/state.json", json_encode($state));
}
?>