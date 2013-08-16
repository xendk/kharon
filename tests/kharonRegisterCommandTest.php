<?php

/**
 * @file
 * Tests the register command.
 */
include_once 'Kharon_CommandTestCase.php';

class KharonRegisterCommandCase extends Kharon_CommandTestCase {
  public function setUp() {
    $this->setUpDrupal(3);
  }

  /**
   * Test that registering basically works.
   */
  function testBasic() {
    // We don't have a MySQL server, but register doesn't care.
    $kharon_dir = UNISH_SANDBOX . '/kharon';
    $options = array(
      'kharon' => $kharon_dir . ':host:user:pass',
    );

    // Ensure that register fails when pointed at a random non-Drupal directory.
    $this->drush('kharon-register', array('hades', UNISH_SANDBOX), $options, NULL, NULL, self::EXIT_ERROR);

    // Should work on a proper site though.
    foreach ($this->sites as $env => $def) {
      $import_name = 'eris-' . $env;
      $this->drush('kharon-register 2>&1', array($import_name, $this->webroot(), $env), $options);
      $config_file = $kharon_dir . '/' . $import_name . '/config';
      $this->assertFileExists($config_file);
      $conf = unserialize(file_get_contents($config_file));
      $this->assertEquals($this->webroot(), $conf['remote']);
      $this->assertEquals($env, $conf['subsite']);
    }
  }
}
