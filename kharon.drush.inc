<?php

/**
 * @file
 * Kharon, a drush command for getting site data.
 */

/**
 * Implements hook_drush_command().
 */
function kharon_drush_command() {
  $common_options = array(
    'kharon-server' => 'Server to use.',
    'kharon' => 'Local server setup.',
  );
  $items['kharon-register'] = array(
    'aliases' => array('kr'),
    'description' => 'Register a site.',
    'arguments' => array(
      'name' => 'Name of the site.',
    ),
    'options' => array(
      'path' => 'Path to the site root dir.',
    ) + $common_options,
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

function drush_kharon_register_validate($name = NULL, $remote = NULL, $subsite = NULL) {
  if (empty($name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('You need to provide a name for the site.'));
  }
  if (!preg_match('/^[a-zA-Z0-9_-]+$/', $name)) {
    return drush_set_error('BAD_ARGUMENTS', dt('Invalid name.'));
  }

  if (!drush_get_option('kharon-server', NULL)) {
    // We're the end of the line, validate some more args.
    if (empty($remote)) {
      return drush_set_error('BAD_ARGUMENTS', dt('Missing remote.'));
    }
    if (!($remote = _kharon_parse_remote($where))) {
      return drush_set_error('BAD_ARGUMENTS', dt('Invalid remote.'));
    }
    drush_set_option('kharon-remote', $remote);
  }
}

function drush_kharon_register($name, $remote, $subsite = NULL) {
  if ($server = drush_get_option('kharon-server', NULL)) {
    list($user, $host) = explode('@', $server);
    // We create a fake alias on the fly.
    $alias = array(
      'remote-host' => $host,
      'remote-user' => $user,
    );
    // Forward to server.
    drush_invoke_process($alias, 'kharon-register', array($name, $remote, $subsite), array(), array('interactive' => TRUE));
    return;
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
        if (!drush_shell_exec(_kharon_ssh_command('echo OK'))) {
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
        $list_cmd = _kharon_ssh_command($list_cmd);
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
        $cat_cmd = _kharon_ssh_command($cat_cmd);
      }

      if (drush_shell_exec($cat_cmd)) {
        if ($creds = _kharon_sniff_creds(drush_shell_exec_output())) {
          drush_print_r($creds);
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
function _kharon_ssh_command($command) {
  $ssh_options = "-o PasswordAuthentication=no";
  $remote = drush_get_option('kharon-remote');
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
    list(
      $server['path'],
      $server['mysql']['host'],
      $server['mysql']['user'],
      $server['mysql']['pass'],
      $server['mysql']['prefix']
    ) = explode(':', $string_server);
  }

  if (empty($server['path']) || !file_exists($server['path'])) {
    return drush_set_error('SERVER_ERROR', dt('Invalid server path.'));
  }

  if (empty($server['mysql']['host']) || empty($server['mysql']['user']) || empty($server['mysql']['pass'])) {
    return drush_set_error('SERVER_ERROR', dt('Missing MySQL setup.'));
  }

  // Make sure that prefix is set.
  if (empty($server['mysql']['prefix'])) {
    $server['mysql']['prefix'] = '';
  }

  return $server;
}
