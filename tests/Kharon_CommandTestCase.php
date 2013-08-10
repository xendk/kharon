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
}
