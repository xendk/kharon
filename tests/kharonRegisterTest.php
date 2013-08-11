<?php

/**
 * @file
 * Tests the register command.
 */
include_once 'Kharon_CommandTestCase.php';

class kharonRegisterCase extends Kharon_CommandTestCase {
  public function setUp() {
    $this->setUpDrupal(3);
  }

  /**
   * Test that registering basically works.
   */
  function testBasic() {
    // $options = array(
    //   'kharon' => array(
    //     'path' => UNISH_SANDBOX . '/kharon',
    //   ),
    // );

    $options = array();
    // Ensure that register fails when pointed at a random non-Drupal directory.
    $this->drush('kharon-register', array('hades', UNISH_SANDBOX), $options, NULL, NULL, self::EXIT_ERROR);

    // Should work on a proper site though.  However, this currenly fails as the
    // site that wes built for the test was faked to the point of having an
    // empty settings file. We'll need to come up with something better (the
    // easiest option is to rewrite the settings file to have some fake
    // settings).
    foreach ($this->sites as $env => $def) {
      $this->drush('kharon-register 2>&1', array('eris', $this->webroot(), $env), $options);
    }
  }
}
