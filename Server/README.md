# Server-side code

This stuff should reside under /opt/saltScale, except for the "value.php" script which should be in the web tree, as /saltScale/value.php - and of course, update URLBASE in RadioScaleReceiver to match your server name, and the path if you change it. 

Run bin/notify.php from cron, once a day or so.

Make sure scripts are readable by the user account(s) who needs to read them, and make sure data/state.json and data/saltScale.txt are writable by the www server user and the crontab owner.

Edit the conf file to suit your needs.

All the file paths are hardcoded, but there's not a lot of code so it's easy to change.
