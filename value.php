<?php
$email_recipient="foo@bar.com";
file_put_contents("/opt/saltScale/data/saltScale.txt", date("c") . "\t" . $_GET["v"] . "\n", FILE_APPEND | LOCK_EX);
$value = $_GET["v"] + 0;
$string = file_get_contents("/opt/saltScale/data/state.json");
$state = json_decode($string, true);
$newstate = "";
$levels = array_keys($state["thresholds"]);
sort($levels);
foreach ($levels as $key) {
    if ($value < $key) {
       $newstate = $state["thresholds"][$key];
       break;
    }
    $highest = $state["thresholds"][$key];
}
if (empty($newstate)) {
  $newstate = $highest;
}

if (strcmp($newstate, $state["status"])) {
   mail($email_recipient,"Salt level is ".$newstate,"The current total weight is " . $value . "kg.");
   $state["status"] = $newstate;
   file_put_contents("/opt/saltScale/data/state.json", json_encode($state));
}
?>
