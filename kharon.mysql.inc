<?php

/**
 * @file
 * MySQL handling for Kharon.
 */

class MySQLTmpDb {
  protected $host = NULL;
  protected $user = NULL;
  protected $pass = NULL;
  protected $name = NULL;
  protected $database = NULL;
  protected $db = NULL;

  /**
   * Create new tmp db.
   */
  public function __construct($db_settings, $name) {
    $this->host = $db_settings['host'];
    $this->user = $db_settings['user'];
    $this->pass = $db_settings['pass'];
    $this->prefix = $db_settings['prefix'];
    $this->name = $name;
    $this->database = $this->prefix . preg_replace('/[^a-z0-9_]/', '_', $name) . '_' . getmypid();
    try {
      $this->db = new PDO('mysql:host=' . $this->host . ';charset=utf8', $this->user, $this->pass);
      if (!$this->db->query('CREATE DATABASE ' . $this->database)) {
        // Shouldn't happen.
        throw new PDOException(dt('Unknown error creating database "@database".', array('@database' => $this->database)));
      }
    }
    catch (PDOException $e) {
      $this->cleanup();
      throw $e;
    }
  }

  /**
   * Return PDO object for this database.
   */
  public function pdo() {
    return $this->db;
  }

  /**
   * Get the name of the database.
   */
  public function name() {
    return $this->database;
  }

  /**
   * Clean up.
   */
  public function cleanup() {
    if ($this->db) {
      try {
        $this->db->query('DROP DATABASE ' . $this->database);
      }
      catch (Exception $e) {
        // Ignore any errors.
      }
    }
  }

  /**
   * Import an SQL file into the temporary database.
   */
  public function import($file) {
    $command = 'mysql 2>&1 ' . $this->my_creds() . ' ' . $this->database . ' --silent <' . drush_escapeshellarg($file);

    if (!drush_shell_exec($command)) {
      throw new PDOException(implode("\n", drush_shell_exec_output()));
    }
  }

  /**
   * Returns creds as string of arguments for the mysql command.
   */
  protected function my_creds() {
    return kharon_mysql_creds($this->host, $this->user, $this->pass);
  }

  /**
   * Dump the database into a SQL file.
   */
  public function dump($file) {
    $command = kharon_mysqldump_comamnd($this->host, $this->user, $this->pass, $this->database) . ' >' . drush_escapeshellarg($file);;
    if (!drush_shell_exec($command)) {
      throw new PDOException(implode("\n", drush_shell_exec_output()));
    }
  }
}

/**
 * Construct a mysqldump command.
 */
function kharon_mysqldump_comamnd($host, $user, $pass, $database) {
  return 'mysqldump 2>&1 --no-autocommit --single-transaction --opt -Q ' . kharon_mysql_creds($host, $user, $pass) . ' ' . $database;
}

/**
 * Returns given creds as string of arguments for the mysql command.
 */
function kharon_mysql_creds($host = NULL, $user = NULL, $pass = NULL) {
  $creds = array();
  if ($host) {
    $creds[] = '--host=' . drush_escapeshellarg($host);
  }
  if ($user) {
    $creds[] = '--user=' . drush_escapeshellarg($user);
  }
  // Empty password is valid.
  if ($pass || $pass == '') {
    $creds[] = '--password=' . drush_escapeshellarg($pass);
  }
  return implode(' ', $creds);
}
