<?php

/**
 * @file
 * Kharon, a drush command for getting site data.
 */

include_once dirname(__FILE__) . '/kharon.mysql.inc';

/**
 * Our own exception class.
 */
class KharonException extends Exception {}

/**
 * Implements hook_drush_command().
 */
function kharon_drush_command() {
  $common_options = array(
    'kharon-server' => 'Server to use.',
    'kharon' => 'Local server setup.',
  );

  $items['kharon-init'] = array(
    'aliases' => array('ki'),
    'description' => 'Initialize server directory.',
    'arguments' => array(
      'directory' => 'The directory to initialize.',
    ),
    'options' => array(
      'mysql-host' => 'Hostname of MySQL server.',
      'mysql-user' => 'MySQL user name.',
      'mysql-pass' => 'MySQL password.',
      'mysql-prefix' => 'Prefix for temporary databases.',
    ),
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['kharon-register'] = array(
    'aliases' => array('kr'),
    'description' => 'Register a site.',
    'arguments' => array(
      'name' => 'Name of the site.',
    ),
    'options' => $common_options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['kharon-list'] = array(
    'aliases' => array('kl'),
    'description' => 'List sites or dumps.',
    'arguments' => array(
      'name' => 'List dumps for site.',
    ),
    'options' => $common_options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['kharon-fetch'] = array(
    'aliases' => array('kf'),
    'description' => 'Dump site to server.',
    'arguments' => array(
      'name' => 'Name of the site.',
    ),
    'options' => $common_options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['kharon-get'] = array(
    'aliases' => array('kg'),
    'description' => 'Get site from server.',
    'arguments' => array(
      'name' => 'Name of the site.',
    ),
    'options' => $common_options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
  );

  $items['kharon-sniff'] = array(
    'aliases' => array('kn'),
    'description' => 'Sniff a site.',
    'arguments' => array(
      'where' => 'User@host/path to sniff.',
      'what' => 'What to sniff, ssh, sites or db',
    ),
    'options' => $common_options,
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH,
    'hidden' => TRUE,
  );

  return $items;
}

/**
 * Valiation for the init command.
 */
function drush_kharon_init_validate($dir = NULL) {
  if (empty($dir)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Need a directory to initialize.'));
  }
  if (file_exists($dir)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Directory already exists.'));
  }
}

/**
 * Init command callback.
 *
 * Makes the server dir and generates a SSH key.
 */
function drush_kharon_init($dir) {
  if (!mkdir($dir)) {
    return drush_set_error('CANNOT_MAKE_DIR', dt('Cannot make directory.'));
  }
  // @todo look into -O clear for clearing permissions. Looks like we can
  //   enforce that an agent forwards the key.
  if (!drush_shell_exec('ssh-keygen -N "" -C "Kharon (%s@%s)" -qf %s', getenv('USER'), php_uname('n'), $dir . '/key')) {
    drush_register_file_for_deletion($dir);
    $output = implode("\n", drush_shell_exec_output());
    return drush_set_error('SSH_KEYGEN_ERROR', dt('Error generating ssh key: @error', array('@error' => $output)));
  }

  $mysql_host = drush_get_option('mysql-host', NULL);
  if (!$mysql_host) {
    $mysql_host = drush_prompt('MySQL host', NULL, TRUE);
  }
  if (!$mysql_host) {
    drush_register_file_for_deletion($dir);
    return drush_user_abort();
  }

  $mysql_user = drush_get_option('mysql-user', NULL);
  if (!$mysql_user) {
    $mysql_user = drush_prompt('MySQL user', NULL, TRUE);
  }
  if (!$mysql_user) {
    drush_register_file_for_deletion($dir);
    return drush_user_abort();
  }

  $mysql_pass = drush_get_option('mysql-pass', NULL);
  if (is_null($mysql_pass)) {
    $mysql_pass = drush_prompt('MySQL pass', '', FALSE, TRUE);
  }

  $mysql_prefix = drush_get_option('mysql-prefix', NULL);
  if (is_null($mysql_prefix)) {
    $mysql_prefix = drush_prompt('MySQL database prefix', '', FALSE);
  }

  // Let's check that we can use the creds.
  // @todo Direct the user to some help that mentions grant all on `kharon\_%`.*
  //   to kharon@localhost identified by 'obolus';
  try {
    $pdo = new PDO('mysql:host=' . $mysql_host . ';charset=utf8', $mysql_user, $mysql_pass);
  }
  catch (PDOException $e) {
    drush_register_file_for_deletion($dir);
    return drush_set_error('MYSQL_ERROR', dt('Could not connect to database, please check credentials and grants.'));
  }

  if (!$pdo->query('CREATE DATABASE ' . $mysql_prefix . '_init_test')) {
    drush_register_file_for_deletion($dir);
    return drush_set_error('MYSQL_ERROR', dt('Could not create a test databse, please check credentials and grants.'));
  }

  if (!$pdo->query('DROP DATABASE ' . $mysql_prefix . '_init_test')) {
    drush_register_file_for_deletion($dir);
    return drush_set_error('MYSQL_ERROR', dt('Could not drop test databse, please check credentials and grants.'));
  }

  // Checks out, let's save.
  $config = array(
    'mysql' => array(
      'host' => $mysql_host,
      'user' => $mysql_user,
      'pass' => $mysql_pass,
      'prefix' => $mysql_prefix,
    ),
  );

  if (!file_put_contents($dir . '/config', serialize($config))) {
    return drush_set_error('ERROR_SAVING_CONFIG', dt('Error saving config file.'));
  }

  drush_log(dt('Server directory created.'), 'ok');
}

/**
 * Validation for the register command.
 */
function drush_kharon_register_validate($name = NULL, $remote = NULL, $subsite = NULL) {
  if (empty($name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('You need to provide a name for the site.'));
  }
  if (!preg_match('/^[.a-zA-Z0-9_-]+$/', $name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Invalid name.'));
  }

  // Check remote before forwarding.
  if (empty($remote)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Missing remote.'));
  }

  if (!($remote = _kharon_parse_remote($remote))) {
    return drush_set_error('BAD_ARGUMENTS', dt('Invalid remote.'));
  }
}

/**
 * Register command callback.
 *
 * Registers a new site server side.
 */
function drush_kharon_register($name, $remote  = NULL, $subsite = NULL) {
  if (_kharon_is_client()) {
    return _kharon_forward_to_server('kharon-register', func_get_args(), array());
  }
  else {
    // Ensure we're properly set up as a server.
    $server = _kharon_validate_server();
    if (!$server) {
      // _kharon_validate_server already set the error.
      return $server;
    }
  }

  $options = array(
    'kharon' => drush_get_option('kharon', NULL),
  );
  $parsed_remote = _kharon_parse_remote($remote);

  // First check that we can connect to the site.
  $res = drush_invoke_process('@none', 'kharon-sniff', array($remote, 'ssh'), $options, array('integrate' => TRUE));
  if ($res['error_status'] != 0) {
    // In the case of authentication error, print some more help.
    if (!empty($res['error_log']['SSH_KEY_ERROR'])) {
      drush_print();
      drush_print(dt("You have no obolus..."));
      drush_print();
      drush_print(dt('Please add the Kharon key to authorized keys on the target machine. This should do it:'));
      drush_print();
      drush_print('echo \'' . trim(file_get_contents($server['path'] . '/key.pub')) . '\' | ssh ' . $parsed_remote['user'] . '@' . $parsed_remote['host'] . ' \'mkdir -p $HOME/.ssh && cat >>$HOME/.ssh/authorized_keys\'');
      // @todo: print something about how to get the key to the relevant
      //   server.
    }
    return FALSE;
  }

  // Check sites.
  $res = drush_invoke_process('@none', 'kharon-sniff', array($remote, 'sites'), array(), array('integrate' => TRUE, 'output' => FALSE));
  if ($res['error_status'] != 0) {
    drush_print();
    drush_print(dt('Could not get list of sub-sites, sure it is the right path?'));
    return FALSE;
  }

  $sites = $res['object'];
  $select = FALSE;
  if (empty($subsite)) {
    // If subsite was not specified, if there is only one, select that, else ask
    // user.
    if (count($sites) == 1) {
      $subsite = reset($sites);
    }
    else {
      $select = 'There is more than one sub-site, please select one:';
    }
  }
  elseif (!in_array($subsite, $sites)) {
    drush_print('here');
    // Let user choose another if the specified does not exist.
    $select = "Specified sub-site does not exist, please select another:";
  }

  if ($select) {
    $subsite = drush_choice(array_combine($sites, $sites), $select);
  }

  if (!$subsite) {
    return drush_user_abort();
  }

  // Now we have all the information we need for now. The final check is to see
  // if we can get the database credentials.
  $args = array($remote, 'db', $subsite);
  $res = drush_invoke_process('none', 'kharon-sniff', $args, array(), array('integrate' => TRUE, 'output' => FALSE));
  if ($res['error_status'] != 0) {
    if (!empty($res['error_log']['DB_SNIFF_READ_ERROR'])) {
      drush_print();
      drush_print(dt("Trouble reading the settings.php file, please chech permissions of the file."));
    }
    elseif (!empty($res['error_log']['DB_SNIFF_PARSE_ERROR'])) {
      drush_print();
      drush_print(dt("Could not sniff the databse credentials from the settings file."));
    }
    else {
      drush_print();
      drush_print(dt("Something bad happened, and I have no idea what. Could be a parse error in settings.php, but that's the only guess I can make."));
    }
    return FALSE;
  }
  $creds = $res['object'];

  // And lets just check that is uses mysql.
  if (!in_array($creds['driver'], array('mysql', 'mysqli'))) {
    drush_set_error('UNSUPPORTED_DB', dt('Only mysql is currently supported.'));
  }

  // Everything seems to check out, let us try to save the new site.
  $dir = $server['path'] . '/' . $name;
  if (@mkdir($dir)) {
    $config = array(
      'remote' => $remote,
      'subsite' => $subsite,
    );
    if (!file_put_contents($dir . '/config', serialize($config))) {
      rmdir($dir);
      return drush_set_error('CANNOT_WRITE_CONFIG', dt('Error creating config file. Aborting.'));
    }
  }
  elseif (file_exists($dir)) {
    return drush_set_error('ALREADY_EXISTS', dt('"@name" already exists.', array('@name' => $name)));
  }
  else {
    return drush_set_error('CANNOT_MAKE_DIR', dt('Cannot make a "@name" directory in the server dir. Please check permissions.', array('@name' => $name)));
  }
}

/**
 * Validation for the list command.
 */
function drush_kharon_list_validate($name = NULL) {
  if (!_kharon_is_client()) {
    $server = _kharon_validate_server();
    if (!$server) {
      // _kharon_validate_server already set the error.
      return FALSE;
    }

    if (!empty($name)) {
      if (!file_exists($server['path'] . '/' . $name . '/config')) {
        return drush_set_error('DONT_EXISTS', dt('No such site.'));
      }
    }
  }
}

/**
 * Command callback for the kharon-list command.
 *
 * List sites or dumps in site.
 */
function drush_kharon_list($name = NULL) {
  // Forward upstream, if we're not the server.
  if (_kharon_is_client()) {
    return _kharon_forward_to_server('kharon-list', func_get_args(), array());
  }

  $server = _kharon_validate_server();
  if (!$server) {
    // _kharon_validate_server already set the error.
    return FALSE;
  }
  if (empty($name)) {
    // List sites, if no name given.
    $sites = _kharon_find_sites($server['path']);
    ksort($sites);
    $rows = array();
    foreach ($sites as $name => $config) {
      $rows[] = array($name);
    }
    drush_print_table($rows);
    return $sites;
  }
  else {
    // List dumps in site.
    $dumps = _kharon_find_dumps($server['path'] . '/' . $name);
    ksort($dumps);
    $rows = array();
    foreach ($dumps as $name => $config) {
      $rows[] = array($name);
    }
    drush_print_table($rows);
    return $dumps;
  }
}

/**
 * Validation for the fetch command.
 */
function drush_kharon_fetch_validate($name = NULL) {
  if (empty($name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('You need to provide a site to fetch.'));
  }

  if (!_kharon_is_client()) {
    // We're the end of the line, validate some more args.
    if (!($server = _kharon_validate_server())) {
      return FALSE;
    }

    if (!file_exists($server['path'] . '/' . $name . '/config')) {
      return drush_set_error('DONT_EXISTS', dt('No such site.'));
    }
  }
}

/**
 * Command callback for the kharon-fetch command.
 *
 * Fetches database and files into local dir.
 */
function drush_kharon_fetch($name) {
  if (_kharon_is_client()) {
    return _kharon_forward_to_server('kharon-fetch', func_get_args(), array());
  }

  $server_settings = drush_get_option('kharon-settings', NULL);
  $site_path = $server_settings['path'] . '/' . $name;
  $config = _kharon_load_site_config($site_path . '/config');
  if (!$config) {
    return FALSE;
  }

  $args = array($config['remote'], 'db', $config['subsite']);
  $res = drush_invoke_process('none', 'kharon-sniff', $args, array(), array('integrate' => TRUE, 'output' => FALSE));
  if ($res['error_status'] != 0) {
    drush_print();
    drush_print(dt("Error fetching database credentials."));
    return FALSE;
  }

  $creds = $res['object'];

  // Locking could be sensible, but currently it's of marginal value.
  // $fp = fopen($site_path . '/status', "r");
  // if (flock($fp, LOCK_EX | LOCK_NB)) {
  //   flock($fp, LOCK_UN);
  // }
  // else {
  //   drush_set_error('CANNOT_LOCK', dt('Could not obtain site lock. Is there another fetch running?'));
  // }

  // Info for the info file. (Yeah, really descriptive, I know).
  $dump_info = array(
    'timestamp' => (string) time(),
    'core' => $creds['core'],
    'file dirs' => array(),
  );

  $dump_name = $dump_info['timestamp'];
  $tmp_name = $dump_name . '.' . getmypid();
  $tmp_dir = $site_path . '/' . $tmp_name;

  if (!mkdir($tmp_dir)) {
    return drush_set_error('FETCH_ERROR', dt('Could not create dump dir.'));
  }
  // Let Drush clean up the tmp dir if we exit prematurely.
  drush_register_file_for_deletion($tmp_dir);

  $tmp_file = $tmp_dir . '/tmp.sql';
  // Dump remote database into a temporary SQL file.
  drush_log(dt('Dumping database from site.'), 'status');
  if (!_kharon_dump_db($tmp_file, $creds, $config)) {
    return FALSE;
  }

  // To improve readability later on.
  $mysql = $server_settings['mysql'];
  $mysql['driver'] = 'mysql';
  $db = NULL;

  try {
    $db = kharon_get_tmp_db($mysql, $name);
    $db->import($tmp_file);
    // Remove temporary dump file.
    unlink($tmp_file);

    // Figure out the files directories from the database and sync the files.
    $file_dirs = _kharon_file_dirs($config, $creds, $db);
    $remote = _kharon_parse_remote($config['remote']);
    $prefix = '';
    if (!$remote['is_local']) {
      $prefix = $remote['user'] . '@' . $remote['host'] . ':';
    }
    // Find previous dumps for hardlinking files.
    $previous_dumps = _kharon_find_dumps($site_path);
    foreach ($file_dirs as $name => $path) {
      drush_log(dt('Syncing @type files.', array('@type' => $name)), 'status');

      $link_dests = array();
      foreach ($previous_dumps as $prev_dump_name => $info) {
        if (file_exists($info['path'] . '/' . $name)) {
          drush_log(dt('Hardlinking with @dir', array('@dir' => $info['path'] . '/' . $name)), 'debug');
          $link_dests[] = '--link-dest=' . drush_escapeshellarg($info['path'] . '/' . $name);
        }
      }
      if (!drush_shell_exec('rsync -a ' . implode(' ', $link_dests) . ' %s %s', $prefix . ($path[0] == '/' ? '' : $remote['path'] . '/') . $path . '/', $tmp_dir . '/' . $name)) {
        return drush_set_error('RSYNC_ERROR', dt('Error copying files.'));
      }
      $dump_info['file dirs'][] = $name;
    }
    // Clean up file paths in the db.
    // Run scripts.

    $db->dump($tmp_dir . '/database.sql');
    if (!drush_shell_exec('gzip %s', $tmp_dir . '/database.sql')) {
      return drush_set_error('GZIP_ERROR', dt('Error compressing SQL dump.'));
    }
    $dump_info['database'] = 'database.sql.gz';
    $dump_info['compression'] = 'gzip';
    $db->cleanup();
  }
  catch (PDOException $e) {
    // @todo $db->cleanup()
    return drush_set_error('DB_ERROR', dt('Database error: @message', array('@message' => $e->getMessage())));
  }
  catch (KharonException $e) {
    // @todo $db->cleanup()
    return drush_set_error('KHARON_ERROR', $e->getMessage());
  }

  // Save some info to ease restoring.
  if (FALSE === file_put_contents($tmp_dir . '/info', serialize($dump_info))) {
    return drush_set_error('ERROR_SAVING_INFO', dt('Error saving info file.'));
  }

  // Move new dump into place.
  rename($tmp_dir, $site_path . '/' . $dump_name);
}

/**
 * Valiation for the get command.
 */
function drush_kharon_get_validate($site = NULL, $dump = NULL) {
  if (empty($site)) {
    return drush_set_error('BAD_ARGUMENTS', dt('You need to provide a site to fetch.'));
  }

  // @todo @self is probably not good enough. Drush has some context for the
  //   implicit/specified alias.
  $res = drush_invoke_process('@self', 'sql-conf', array(), array('show-passwords' => TRUE), array('integrate' => FALSE, 'output' => FALSE));
  if ($res['error_status'] != 0) {
    return drush_set_error('NO_LOCAL_SITE', dt("You need to be a site context to get a dump.\n\nEither cd to a site directory, or specify an alias on the command line."));
  }

  drush_set_option('kharon-creds', $res['object']);
}

/**
 * Get command callback.
 *
 * Fetches a dump into a local site.
 */
function drush_kharon_get($site, $dump = NULL) {
  $args = array();
  $local = TRUE;
  // Pass on remote or server option, depending.
  if ($server = drush_get_option('kharon-remote')) {
    $args['kharon-remote'] = $server;
    $local = FALSE;
  }
  elseif ($server = drush_get_option('kharon')) {
    $args['kharon'] = $server;
  }
  else {
    return drush_set_error('NO_SERVER', dt('Neither remote nor local server specified.'));
  }
  $res = drush_invoke_process('none', 'kharon-list', array($site), $args, array('integrate' => TRUE, 'output' => FALSE));
  if ($res['error_status'] != 0) {
    drush_print();
    drush_print(dt("Error fetching database credentials."));
    return FALSE;
  }

  $dumps = $res['object'];

  if (count($dumps) < 1) {
    return drush_set_error('NO_DUMPS', dt('No dumps created yet.'));
  }

  if (!empty($dump)) {
    if (!isset($dumps[$dump])) {
      return drush_set_error('DONT_EXISTS', dt('No such dump.'));
    }
    $get_dump = $dumps[$dump];
  }
  else {
    // Sort dumps by timestamp.
    uasort($dumps, '_kharon_sort_dumps_by_timestamp');
    $get_dump = end($dumps);
  }

  // Absolute path to the site folder.
  $site_path = drush_sitealias_local_site_path(drush_sitealias_get_record('@self'));
  $parts = explode('/', $site_path);

  // Path to site folder relative to Drupal root.
  $in_site_path = implode('/', array_reverse(array_slice(array_reverse($parts), 0, 2)));

  // Creds for the local database.
  $local_creds = drush_get_option('kharon-creds', array());

  if (!file_exists($site_path)) {
    return drush_set_error('SITE_ERROR', dt('Problem figuring out local site path.'));
  }

  if ($get_dump['info']['compression'] != 'gzip') {
    return drush_set_error('COMP_ERROR', dt('Unsupported compression "@compression" used in dump.', array('@compression' => $get_dump['info']['compression'])));
  }

  $db_file = $get_dump['path'] . '/' . $get_dump['info']['database'];
  if ($local) {
    // Get database.
    // @todo Mysql lib should be responsible for the mysql command line.
    $command = 'cat %s | gunzip -c | mysql ' . kharon_mysql_creds($local_creds['host'], $local_creds['username'], $local_creds['password']) . ' ' . $local_creds['database'];
    if (!drush_shell_exec($command, $db_file)) {
      return drush_set_error('GET_ERROR', dt('Error getting database dump.'));
    }
  }
  else {
    return drush_set_error('NOT_IMPLEMENTED', dt('Getting remotely not quite implemented yet.'));
  }

  // Get files.
  $file_paths = array();

  foreach ($get_dump['info']['file dirs'] as $file_path) {
    $source_path = $get_dump['path'] . '/' . $file_path;
    if (!drush_shell_exec('rsync -a %s %s', $source_path . '/', $site_path . '/files/' . $file_path)) {
      return drush_set_error('RSYNC_ERROR', dt('Error copying files.'));
    }
    $file_paths[$file_path] = $in_site_path . '/files/' . $file_path;
  }

  // Add the core version to the creds.
  $local_creds['core'] = $get_dump['info']['core'];
  // Set variables/config pointing to file dirs.
  if (!_kharon_set_file_dirs($local_creds, $file_paths)) {
    return FALSE;
  }

  $res = drush_invoke_process('@self', 'cc', array('all'), array(), array('integrate' => TRUE, 'output' => TRUE));
  if ($res['error_status'] != 0) {
    return drush_set_error('ERROR_CLEARING', dt('Error clearing cache in local site.'));
  }
}

/**
 * Validation for the sniff command.
 */
function drush_kharon_sniff_validate($where = NULL, $what = NULL, $arg = NULL) {
  $error = FALSE;
  if (empty($where)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Missing "where".'));
  }
  else {
    if (!($remote = _kharon_parse_remote($where))) {
      return drush_set_error('BAD_ARGUMENTS', dt('Invalid "where".'));
    }
    drush_set_option('kharon-remote', $remote);
  }
  if (empty($what)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Missing "what".'));
  }
  else {
    if (!in_array($what, array('ssh', 'sites', 'db'))) {
      return drush_set_error('BAD_ARGUMENTS', dt('Invalid "what".'));
    }
  }
  if ($what == 'db' && empty($arg)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Missing sub-site.'));
  }
}

/**
 * Command callback for the kharon-sniff command.
 *
 * Does various tests and sniffing on the target site.
 */
function drush_kharon_sniff($where, $what, $arg = NULL) {
  $remote = drush_get_option('kharon-remote');
  switch ($what) {
    case 'ssh':
      // When local, this is a no-op. When not we try to ssh in and see what
      // happns.
      if (!$remote['is_local']) {
        if (!drush_shell_exec(_kharon_ssh_command($remote, 'echo OK'))) {
          $output = implode("\n", drush_shell_exec_output());
          if (preg_match('/Permission denied/', $output)) {
            return drush_set_error('SSH_KEY_ERROR', dt('Public key rejected.'));
          }
          elseif (preg_match('/Could not resolve hostname/', $output)) {
            return drush_set_error('SSH_ERROR', dt('Could not resolve hostname "@host".', array('@host' => $remote['host'])));
          }
          else {
            return drush_set_error('SSH_ERROR', dt('Error from SSH: @error', array('@error' => $output)));
          }
        }
      }
      break;

    case 'sites':
      $list_cmd = sprintf('cd %s && [ -d "sites" ] && for x in `ls sites`; do if [ -e "sites/$x/settings.php" ]; then echo $x;fi ; done', $remote['path']);

      if (!$remote['is_local']) {
        $list_cmd = _kharon_ssh_command($remote, $list_cmd);
      }

      if (drush_shell_exec($list_cmd)) {
        foreach (drush_shell_exec_output() as $line) {
          drush_print($line);
        }
        // Return list if invoked by other command.
        return drush_shell_exec_output();
      }
      else {
        drush_set_error('SITES_ERROR', dt('Problem retrieving sites.'));
      }
      break;

    case 'db':
      $cat_cmd = sprintf('cat %s', $remote['path'] . '/sites/' . $arg . '/settings.php');

      if (!$remote['is_local']) {
        $cat_cmd = _kharon_ssh_command($remote, $cat_cmd);
      }

      if (drush_shell_exec($cat_cmd)) {
        if ($creds = _kharon_sniff_creds(drush_shell_exec_output())) {
          $rows = array();
          foreach ($creds as $name => $val) {
            $rows[] = array($name, ':', $val);
          }
          drush_print_table($rows);
          return $creds;
        }
        else {
          drush_set_error('DB_SNIFF_PARSE_ERROR', dt('Could not sniff database credentials.'));
        }
      }
      else {
        drush_set_error('DB_SNIFF_READ_ERROR', dt('Problem retrieving settings.php.'));
      }
      break;

    default:
      drush_set_error('NOT_IMPLEMENTED', dt('"@what" not implemented.', array('@what' => $what)));
  }
}

/**
 * Get a temporary database object.
 */
function kharon_get_tmp_db($db_settings, $name) {
  // Only MySQL currenly supported, but we might add support for more in the
  // future.
  if (in_array($db_settings['driver'], array('mysql', 'mysqli'))) {
    return new MySQLTmpDb($db_settings, $name);
  }
  else {
    return drush_set_error('MYSQL_ERROR', dt('Database "@driver" not supported.', array('@driver' => $db_settings['driver'])));
  }
}

/**
 * Parse an remote string.
 */
function _kharon_parse_remote($remote) {
  if (!preg_match('{^(?P<login>(?P<user>[^@]+)@(?P<host>[^:]+):)?(?P<path>.*)$}', $remote, $matches)) {
    return NULL;
  }

  return array(
    'user' => $matches['user'],
    'host' => $matches['host'],
    'path' => $matches['path'],
    'is_local' => empty($matches['login']) || ($matches['user'] == getenv('USER') && $matches['host'] == 'localhost'),
  );
}

/**
 * Wraps a command in a ssh command.
 */
function _kharon_ssh_command($remote, $command) {
  // Disable agent forwarding, password authentication and strict host key
  // checking. Strict host key checking wont allow us to connect to a new host
  // when running non-interactively.
  $ssh_options = "-a -o PasswordAuthentication=no -o StrictHostKeyChecking=no";
  $user = drush_escapeshellarg($remote['user']);
  $host = drush_escapeshellarg($remote['host']);
  return sprintf('ssh %s %s@%s %s', $ssh_options, $user, $host, drush_escapeshellarg($command));
}

/**
 * Tries to figure out the database credentials from settings.
 *
 * @param array $settings
 *   The lines of the settings file, as an array.
 */
function _kharon_sniff_creds($settings) {
  // Get rid of the <?php at the start.
  array_shift($settings);
  eval(implode("\n", $settings));

  $creds = array();
  // These was originally lifted from Drush bootstrap.php.
  if (!empty($databases['default']['default'])) {
    // Drupal 7/8.
    $conn = $databases['default']['default'];
    // Fill in defaults to prevent notices.
    $conn += array(
      'username' => NULL,
      'host' => NULL,
      'port' => NULL,
      'password' => NULL,
      'database' => NULL,
      'unix_socket' => NULL,
    );
    // $config_directories was introduced in D8.
    $creds['core'] = isset($config_directories) ? '8.x' : '7.x';
    $creds['driver'] = $conn['driver'];
    $creds['username'] = $conn['username'];
    // $creds['unix_socket'] = $conn['unix_socket'];
    $creds['hostname'] = $conn['host'];
    // $creds['port'] = $conn['port'];
    $creds['database'] = $conn['database'];
    $creds['password'] = $conn['password'];
  }
  elseif (!empty($db_url)) {
    // Drupal 6.
    if (is_array($db_url)) {
      $db_url = $db_url['default'];
    }
    $parts = parse_url($db_url);
    // Fill in defaults to prevent notices.
    $parts += array('pass' => '', 'port' => '');
    $creds['core'] = '6.x';
    $creds['driver'] = $parts['scheme'];
    $creds['username'] = urldecode($parts['user']);
    $creds['hostname'] = $parts['host'];
    // $creds['port'] = $parts['port'];
    $creds['password'] = urldecode($parts['pass']);
    $creds['database'] = trim($parts['path'], '/');
  }

  return $creds;
}

/**
 * Validate server settings.
 */
function _kharon_validate_server() {
  $server_path = drush_get_option('kharon', NULL);
  if (!$server_path) {
    return drush_set_error('SERVER_ERROR', dt('Server not configured.'));
  }

  if (empty($server_path)) {
    return drush_set_error('SERVER_ERROR', dt('Invalid server path.'));
  }
  if (!file_exists($server_path)) {
    return drush_set_error('SERVER_ERROR', dt('Server path does not exist.'));
  }

  if (!file_exists($server_path . '/config')) {
    return drush_set_error('SERVER_ERROR', dt('No config file in server directory.'));
  }
  $server = unserialize(file_get_contents($server_path . '/config'));
  if (empty($server)) {
    return drush_set_error('SERVER_ERROR', dt('Could not load configuration from the config file.'));
  }

  if (isset($server['mysql'])) {
    if (empty($server['mysql']['host']) || empty($server['mysql']['user'])) {
      return drush_set_error('SERVER_ERROR', dt('Missing MySQL setup.'));
    }

    // Make sure that prefix is set.
    if (empty($server['mysql']['prefix'])) {
      $server['mysql']['prefix'] = '';
    }

    // And password.
    if (empty($server['mysql']['pass'])) {
      $server['mysql']['pass'] = '';
    }
  }
  else {
    return drush_set_error('SERVER_ERROR', dt('No MySQL configuration in config file.'));
  }
  $server['path'] = $server_path;

  drush_set_option('kharon-settings', $server);
  return $server;
}

/**
 * Determine whether we're running as a client, or server.
 */
function _kharon_is_client() {
  $server = drush_get_option('kharon-server', NULL);
  return !empty($server);
}

/**
 * Run command at server.
 */
function _kharon_forward_to_server($command, $args, $options) {
  drush_log('Forwarding to server.', 'notice');
  if ($res = _kharon_server_invoke($command, $args, $options)) {
    return $res['error_status'];
  }
  else {
    return FALSE;
  }
}

/**
 * Invoke a command at the server.
 */
function _kharon_server_invoke($command, $args, $options) {
  if ($server = drush_get_option('kharon-server', NULL)) {
    $remote = _kharon_parse_remote($server);
    if ($remote) {
      // We create a fake alias on the fly.
      $alias = array(
        'remote-host' => $remote['host'],
        'remote-user' => $remote['user'],
      );
      $options['kharon'] = $remote['path'];
      // Forward to server.
      $res = drush_invoke_process($alias, $command, $args, $options, array('interactive' => TRUE));
      return $res;
    }
  }
  return drush_set_error('BAD_SERVER', dt('Invalid or missing server setup.'));
}

/**
 * Loads and validates a config file.
 */
function _kharon_load_site_config($file) {
  $config = unserialize(file_get_contents($file));
  if (empty($config['remote']) || empty($config['subsite'])) {
    return drush_set_error('BAD_CONFIG', dt('Config file mangled.'));
  }

  return $config;
}

/**
 * Dump a (local or remote) site database.
 */
function _kharon_dump_db($outfile, $creds, $config) {
  $dump_command = kharon_mysqldump_comamnd($creds['hostname'], $creds['username'], $creds['password'], $creds['database']);
  // Add --ignore-table when we support exclusions.
  $remote = _kharon_parse_remote($config['remote']);

  if (!$remote['is_local']) {
    drush_print(_kharon_ssh_command($remote, $dump_command));
    // --remove-source-files
  }
  else {
    $dump_command .= ' --result-file ' . $outfile;
    if (!drush_shell_exec($dump_command)) {
      return drush_set_error('DUMP_ERROR', dt('Error dumping database.'));
    }
  }

  // Safetycheck file.
  $last_line = `tail -1 $outfile`;
  if (preg_match('/Dump completed/', $last_line)) {
    return TRUE;
  }
  else {
    return drush_set_error('DUMP_ERROR', dt('Error in dump file.'));
  }
}

/**
 * Drop a temporary database.
 */
function _kharon_db_drop($db, $db_name) {
  try {
    if ($db->query('DROP DATABASE ' . $db_name)) {
      return $db;
    }
    return drush_set_error('PDO_ERROR', dt('Could not drop temporary database, an unspecified error happened.'));
  }
  catch (PDOException $e) {
    return drush_set_error('PDO_ERROR', dt('Caught PDO Exception trying to drop temporary database: @message', array('@message' => $e->getMessage())));
  }
}

/**
 * Returns the paths of the file directories from a temporary database.
 */
function _kharon_file_dirs($config, $creds, $tmp_db) {
  switch ($creds['core']) {
    case '6.x':
      $paths = array(
        'public' => 'file_directory_path',
      );
      break;

    case '7.x':
      $paths = array(
        'private' => 'file_private_path',
        'public' => 'file_public_path',
      );
      break;

    default:
      throw new KharonException(dt('Unsupported core version.'));
  }

  $result = array();
  foreach ($paths as $name => $var) {
    $res = $tmp_db
      ->pdo()
      ->query('SELECT value FROM ' . $creds['prefix'] . 'variable WHERE name = "' . $var . '"');
    if ($res && ($val = $res->fetchColumn())) {
      $result[$name] = unserialize($val);
    }
    elseif ($name == 'public') {
      // Default location if not configured.
      $result[$name] = 'sites/' . $config['subsite'] . '/files';
    }
  }
  return $result;
}

/**
 * Configures the current sites file paths.
 */
function _kharon_set_file_dirs($creds, $file_paths) {
  switch ($creds['core']) {
    case '6.x':
      $paths = array(
        'public' => 'file_directory_path',
      );
      break;

    case '7.x':
      $paths = array(
        'private' => 'file_private_path',
        'public' => 'file_public_path',
      );
      break;

    default:
      throw new KharonException(dt('Unsupported core version.'));
  }

  // Connect to the new local database.
  // @todo This definitely needs to refactored into whatever gets to replace
  //   MySQLTmpDb.
  $db = new PDO('mysql:host=' . $creds['host'] . ';charset=utf8', $creds['username'], $creds['password']);
  if (!$db->query('USE ' . $creds['database'])) {
    // Shouldn't happen.
    throw new PDOException(dt('Unknown error using database "@database".', array('@database' => $creds['database'])));
  }

  foreach ($paths as $name => $var) {
    $stmt = $db->prepare('INSERT INTO ' . $creds['prefix'] . 'variable (name, value) VALUES (:name, :value) ON DUPLICATE KEY UPDATE value=:value');
    if (isset($file_paths[$name])) {
      if (!$stmt->execute(array(':name' => $var, ':value' => serialize($file_paths[$name])))) {
        return drush_set_error('FILE_DIRS', dt('Error setting file setting.'));
      }
    }
  }
}

/**
 * Finds all sites in a server dir.
 */
function _kharon_find_sites($dir) {
  $candidates = drush_scan_directory($dir, '/.*/', array('.', '..', 'CVS'), 0, FALSE);
  $files = array();
  foreach ($candidates as $filename => $file) {
    if (file_exists($filename . '/config')) {
      $files[$file->basename] = array(
        'config' => unserialize(file_get_contents($filename . '/config')),
        'path' => $filename,
      );
    }
  }
  return $files;
}

/**
 * Finds all dumps in a site dir.
 */
function _kharon_find_dumps($dir) {
  $candidates = drush_scan_directory($dir, '/.*/', array('.', '..', 'CVS'), 0, FALSE);
  $files = array();
  foreach ($candidates as $filename => $file) {
    if (file_exists($filename . '/info')) {
      $files[$file->basename] = array(
        'info' => unserialize(file_get_contents($filename . '/info')),
        'path' => $filename,
      );
    }
  }
  return $files;
}

/**
 * Sort dumps by timestamp.
 */
function _kharon_sort_dumps_by_timestamp($a, $b) {
  // Avoid notices.
  $a_stamp = isset($a['info']['timestamp']) ? $a['info']['timestamp'] : 0;
  $b_stamp = isset($b['info']['timestamp']) ? $b['info']['timestamp'] : 0;
  if ($a_stamp == $b_stamp) {
    return 0;
  }
  return ($a_stamp < $b_stamp) ? -1 : 1;
}
