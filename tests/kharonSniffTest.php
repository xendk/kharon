<?php

/**
 * @file
 * Tests the register command.
 */

include_once 'Kharon_CommandTestCase.php';

class kharonSniffCase extends Kharon_CommandTestCase {
  /**
   * Test that ssh sniffing works for local site.
   */
  public function testSshSniff() {
    // Just a plain path.
    $this->drush('kharon-sniff', array(UNISH_SANDBOX . '/kharon', 'ssh'));
    // With user and host.
    $this->drush('kharon-sniff', array(getenv('USER') . '@localhost/' . UNISH_SANDBOX . '/kharon', 'ssh'));
  }

  /**
   * Test that site sniffing works for local site.
   */
  public function testSitesSniff() {
    $sites = $this->setUpDrupal(3);
    $this->drush('kharon-sniff', array($this->webroot(), 'sites'));
    // We should get the same sites as the keys of the sites array.
    $this->assertEmpty(array_diff(array_keys($sites), explode("\n", $this->getOutput())));
  }

  /**
   * Test that db creds sniffing works for local site.
   */
  public function testDbSniff() {
    // This has the same problem as the test for register.
    $this->fail('Implement db test.');
  }
}
