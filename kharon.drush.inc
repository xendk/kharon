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

  $items['kharon-fetch'] = array(
    'aliases' => array('kf'),
    'description' => 'Fetch database and files from a site.',
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
  if (!drush_shell_exec('ssh-keygen -N "" -qf %s', $dir . '/key')) {
    drush_register_file_for_deletion($dir);
    $output = implode("\n", drush_shell_exec_output());
    return drush_set_error('SSH_KEYGEN_ERROR', dt('Error generating ssh key: @error', array('@error' => $output)));
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
  if (!preg_match('/^[a-zA-Z0-9_-]+$/', $name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Invalid name.'));
  }

  if (!_kharon_is_client()) {
    // We're the end of the line, validate some more args.
    if (empty($remote)) {
      return drush_set_error('BAD_ARGUMENTS', dt('Missing remote.'));
    }
    if (!($remote = _kharon_parse_remote($where))) {
      return drush_set_error('BAD_ARGUMENTS', dt('Invalid remote.'));
    }
  }
}

/**
 * Register command callback.
 *
 * Registers a new site server side.
 */
function drush_kharon_register($name, $remote, $subsite = NULL) {
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

  // First check that we can connect to the site.
  $res = drush_invoke_process($alias, 'kharon-sniff', array($remote, 'ssh'), array(), array('integrate' => TRUE));
  if ($res['error_status'] != 0) {
    // In the case of authentication error, print some more help.
    if (!empty($res['error_log']['SSH_KEY_ERROR'])) {
      drush_print();
      drush_print(dt("You have no obolus..."));
      drush_print();
      // @todo: print something about how to get the key so the relevant
      //   server.
    }
    return FALSE;
  }

  // Check sites.
  $res = drush_invoke_process($alias, 'kharon-sniff', array($remote, 'sites'), array(), array('integrate' => TRUE, 'output' => FALSE));
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
  $res = drush_invoke_process($alias, 'kharon-sniff', array($remote, 'db', $subsite), array(), array('integrate' => TRUE, 'output' => FALSE));
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
 * Validation for the fetch command.
 */
function drush_kharon_fetch_validate($name = NULL) {
  if (empty($name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('You need to provide a site to fetch.'));
  }

  if (!_kharon_is_client()) {
    // We're the end of the line, validate some more args.
    if (!($server = _kharon_validate_server())) {
      return $server;
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
  $res = drush_invoke_process($alias, 'kharon-sniff', $args, array(), array('integrate' => TRUE, 'output' => FALSE));
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
  $dump_name = (string) time();
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
  $db_name = $mysql['prefix'] . $name . '_' . getmypid();
  $db = NULL;

  try {
    $db = kharon_get_tmp_db($mysql, $name);
    $db->import($tmp_file);
    // Remove temporary dump file.
    unlink($tmp_file);

    // Figure out the files directories from the database and sync the files.
    $file_dirs = _kharon_file_dirs($config, $creds, $db);
    $remote = _kharon_parse_remote($config['remote']);
    if (!$remote['is_local']) {
      $prefix = $remote['user'] . '@' . $remote['host'] . ':';
    }
    // @todo find previous dumps for hardlinking files.
    foreach ($file_dirs as $name => $path) {
      if (!drush_shell_exec('rsync -av --link-dest=%s %s %s', '/tmp/kharon/ida/1377548792/public', $prefix . ($path[0] == '/' ? '' : $remote['path'] . '/') . $path . '/', $tmp_dir . '/' . $name)) {
        return drush_set_error('RSYNC_ERROR', dt('Error copying files.'));
      }
    }
    // Clean up file paths in the db.
    // Run scripts.

    $db->dump($tmp_dir . '/database.sql');
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

  // Move new dump into place.
  rename($tmp_dir, $site_path . '/' . $dump_name);
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
  // Disable agent forwarding and password authentication.
  $ssh_options = "-a -o PasswordAuthentication=no";
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
  $server = drush_get_option('kharon', NULL);
  if (!$server) {
    return drush_set_error('SERVER_ERROR', dt('Server not configured.'));
  }

  // Support a string notation.
  if (is_string($server)) {
    $string_server = $server;
    $server = array('mysql' => array());
    $parts = explode(':', $string_server);
    if (count($parts) != 5 && count($parts) != 4) {
      return drush_set_error('SERVER_ERROR', dt('Invalid server string.'));
    }
    list(
      $server['path'],
      $server['mysql']['host'],
      $server['mysql']['user'],
      $server['mysql']['pass'],
      $server['mysql']['prefix']
    ) = $parts;
  }

  if (empty($server['path'])) {
    return drush_set_error('SERVER_ERROR', dt('Invalid server path.'));
  }
  if (!file_exists($server['path'])) {
    return drush_set_error('SERVER_ERROR', dt('Server path does not exist.'));
  }

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
  if ($server = drush_get_option('kharon-server', NULL)) {
    list($user, $host) = explode('@', $server);
    // We create a fake alias on the fly.
    $alias = array(
      'remote-host' => $host,
      'remote-user' => $user,
    );
    // Forward to server.
    $res = drush_invoke_process($alias, $command, $args, $options, array('interactive' => TRUE));
    return $res['error_status'];
  }
  else {
    return drush_set_error('BAD_SERVER', dt('Invalid or missing server setup.'));
  }
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
    if (drush_shell_exec($dump_command)) {
      return TRUE;
    }
    else {
      return drush_set_error('DUMP_ERROR', dt('Error dumping database.'));
    }
  }
}

function _kharon_db_drop($db, $db_name) {
  try {
    if ($db->query('DROP DATABASE ' . $db_name)) {
      return $db;
    }
    return drush_set_error('PDO_ERROR', dt('Could not create temporary database, an unspecified error happened.'));
  }
  catch (PDOException $e) {
    return drush_set_error('PDO_ERROR', dt('Caught PDO Exception trying to drop temporary database: @message', array('@message' => $e->getMessage())));
  }
}

function _kharon_file_dirs($config, $creds, $tmp_db) {
  drush_print_r($config);
  switch ($creds['core']) {
    case '6.x':
      $paths = array(
        'public' => 'file_directory_path',
      );
      break;

    case '7.x':
      $paths = array(
        'private' => 'file_private_path',
        'public' => 'file_public_path', // conf_path() . '/files'
      );
      break;

    default:
      throw new KharonException(dt('Unsupported core version.'));
  }

  $result = array();
  foreach ($paths as $name => $var) {
    drush_print($var);
    $res = $tmp_db
      ->pdo()
      ->query('SELECT value FROM variable WHERE name = "' . $var . '"');
    if ($res && ($val = $res->fetchColumn())) {
      drush_print($name);
      $result[$name] = $val;
    }
    elseif ($name == 'public') {
      // Default location if not configured.
      $result[$name] = 'sites/' . $config['subsite'] . '/files';
    }
  }
  return $result;
}
