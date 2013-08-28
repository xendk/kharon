<?php

/**
 * @file
 * Tests the init command.
 */
include_once 'Kharon_CommandTestCase.php';

class KharonInitCommandCase extends Kharon_CommandTestCase {
  /**
   * Test that init works.
   */
  function testBasic() {
    $db_settings = $this->parseUnishDbUrl();

    $kharon_dir = UNISH_SANDBOX . '/kharon';
    $options = array(
      'mysql-host' => $db_settings['host'],
      'mysql-user' => $db_settings['user'],
      'mysql-pass' => $db_settings['pass'],
      'mysql-prefix' => $db_settings['prefix'],
    );

    // Run the init command.
    $this->drush('kharon-init', array($kharon_dir), $options);
    $this->assertTrue(file_exists($kharon_dir));
    $this->assertTrue(file_exists($kharon_dir . '/config'));

    // Check config file.
    $expected_config = array(
      'mysql' => array(
        'host' => $db_settings['host'],
        'user' => $db_settings['user'],
        'pass' => $db_settings['pass'],
        'prefix' => $db_settings['prefix'],
      ),
    );
    $config = unserialize(file_get_contents(($kharon_dir . '/config')));
    $this->assertEquals($expected_config, $config);

    // Check that it errors out if we try to init again.
    $this->drush('kharon-init', array($kharon_dir), $options, NULL, NULL, self::EXIT_ERROR);
  }
}
