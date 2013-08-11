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
    mkdir(UNISH_SANDBOX . '/kharon');
  }

  function setUp() {
    $this->server_options = array(
      'kharon' => array(
        'path' => UNISH_SANDBOX . '/kharon',
      ),
    );
  }

  /**
   * Overrides Drush_CommandTestCase::setUpDrupal().
   *
   * Ensures that the settings files of the created sites contains valid db
   * settings, even when not installing the sites.
   */
  function setUpDrupal($num_sites = 1, $install = FALSE, $version_string = UNISH_DRUPAL_MAJOR_VERSION, $profile = NULL) {
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
  }
}
