<?php

/**
 * @file
 * Base test case class for Kharon tests.
 */

abstract class Kharon_CommandTestCase extends Drush_CommandTestCase {
  public static function setUpBeforeClass() {
    parent::setUpBeforeClass();
    // Copy in the drush command, so the sandbox can find it.
    $cmd = sprintf('cp -r %s %s', escapeshellarg(dirname(dirname(__FILE__))), escapeshellarg(getenv('HOME') . '/.drush/kharon'));
    exec($cmd);
  }

  public function setUp() {
    $this->server_options = array(
      'kharon' => array(
        'path' => UNISH_SANDBOX . '/kharon',
      ),
    );
  }

  /**
   * Parse out UNISH_DB_URL to an array we can use.
   */
  public function parseUnishDbUrl() {
    if (!preg_match('{mysql://(?P<user>[^:]+):(?P<pass>[^@]*)@(?P<host>.*)}', UNISH_DB_URL, $db_settings)) {
      $this->fail('Could not parse db credentials from UNISH_DB_URL.');
    }
    $db_settings['prefix'] = 'unish_kharon_test_';
    return $db_settings;
  }
  /**
   * Initialize a kharon directory and return the command options to use it.
   */
  public function kharonInit() {
    static $num = 1;
    $db_settings = $this->parseUnishDbUrl();

    $kharon_dir = UNISH_SANDBOX . '/kharon' . $num++;
    $options = array(
      'mysql-host' => $db_settings['host'],
      'mysql-user' => $db_settings['user'],
      'mysql-pass' => $db_settings['pass'],
      'mysql-prefix' => $db_settings['prefix'],
    );

    // Run the init command.
    $this->drush('kharon-init', array($kharon_dir), $options);

    return array(
      'kharon' => $kharon_dir,
    );
  }

  public function resetDrupal() {
    unish_file_delete_recursive($this->webroot() . '/sites');
    $this->sites = array();
  }

  /**
   * Overrides Drush_CommandTestCase::setUpDrupal().
   *
   * Ensures that the settings files of the created sites contains valid db
   * settings, even when not installing the sites.
   *
   * Also adds the core version to the sites array as we fake different core
   * versions when faking settings.php.
   */
  public function setUpDrupal($num_sites = 1, $install = FALSE, $version_string = UNISH_DRUPAL_MAJOR_VERSION, $profile = NULL) {
    parent::setUpDrupal($num_sites, $install, $version_string, $profile);
    if (!$install) {
      $i = 0;
      foreach ($this->sites as $env => $def) {
        // Create different core versions of the settings file.
        switch ($i % 3) {
          case 0:
            $this->sites[$env]['core'] = '6.x';
            $settings = "<?php

\$db_url = 'mysql://${env}_user:${env}_pass@${env}_host/${env}_db';
";
            break;

          case 1:
            $this->sites[$env]['core'] = '7.x';
            $settings = "<?php

\$databases = array (
  'default' =>
  array (
    'default' =>
    array (
      'driver' => 'mysql',
      'database' => '${env}_db',
      'username' => '${env}_user',
      'password' => '${env}_pass',
      'host' => '${env}_host',
      'port' => '',
      'prefix' => '',
    ),
  ),
);
";
            break;

          case 2:
            $this->sites[$env]['core'] = '8.x';
            $settings = "<?php

\$databases = array (
  'default' =>
  array (
    'default' =>
    array (
      'driver' => 'mysql',
      'database' => '${env}_db',
      'username' => '${env}_user',
      'password' => '${env}_pass',
      'host' => '${env}_host',
      'port' => '',
      'prefix' => '',
    ),
  ),
);
\$config_directories = array();
";
            break;

        }
        $settings_file = $this->webroot() . '/sites/' . $env . '/settings.php';
        file_put_contents($settings_file, $settings);
        $i++;
      }
    }
    else {
      // Else just add the version string.
      foreach ($this->sites as $env => $def) {
        $this->sites[$env]['core'] = $version_string . '.x';
      }
    }
  }
}
