<?php
// Read configuration and current state
$string = file_get_contents("/opt/saltScale/etc/config.json");
$conf = json_decode($string, true);
$string = file_get_contents("/opt/saltScale/data/state.json");
$state = json_decode($string, true);

// Get the new weight and ensure it's a numeric value
$state["weight"] = $_GET["v"] + 0;

// Go through the list of possible state from lowest to highest
// and determine the current state
$newstate = "";
$levels = array_keys($conf["thresholds"]);
sort($levels);
foreach ($levels as $key) {
    if ($state["weight"] < $key) {
       $newstate = $conf["thresholds"][$key];
       break;
    }
}

// If the state has changed from a lower state to "Normal" and a
// notification has previously been sent (by notify.php), send an
// "all clear" and clear the "notified" flag.
if (!strcmp($state["notified"],"yes") && !strcmp($newstate, "Normal") && strcmp($state["status"], $newstate)) {
   mail($conf["emailrcpt"],"Salt level is ".$newstate,"The current total weight is " . $state["weight"] . "kg.");
   $state["notified"] = "no";
}

// Preserve the new state and the weight
$state["status"] = $newstate;
file_put_contents("/opt/saltScale/data/state.json", json_encode($state));

// Add the weight to the log
file_put_contents("/opt/saltScale/data/saltScale.txt", date("c") . "\t" . $state["weight"] . "\n", FILE_APPEND | LOCK_EX);
?>
