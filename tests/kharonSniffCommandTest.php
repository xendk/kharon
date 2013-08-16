<?php

/**
 * @file
 * Tests the register command.
 */

include_once 'Kharon_CommandTestCase.php';

class KharonSniffCommandCase extends Kharon_CommandTestCase {

  public function setUp() {
    $this->setUpDrupal(3);
  }

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
    $this->drush('kharon-sniff', array($this->webroot(), 'sites'));
    // We should get the same sites as the keys of the sites array.
    $this->assertEmpty(array_diff(array_keys($this->sites), explode("\n", $this->getOutput())));
  }

  /**
   * Test that db creds sniffing works for local site.
   */
  public function testDbSniff() {
    // Ttry all the sites created by setUp().
    foreach ($this->sites as $env => $def) {
      // We'll grab STDERR too, that'll make debugging easier if failing.
      $this->drush('kharon-sniff 2>&1', array($this->webroot(), 'db', $env), array());
      $output = $this->getOutput();
      $this->assertRegExp('{\[driver\] => mysql\n}', $output);
      $this->assertRegExp('{\[core\] => ' . $def['core'] . '\n}', $output);
      $this->assertRegExp('{\[username\] => ' . $env . '_user\n}', $output);
      $this->assertRegExp('{\[hostname\] => ' . $env . '_host\n}', $output);
      $this->assertRegExp('{\[database\] => ' . $env . '_db\n}', $output);
      $this->assertRegExp('{\[password\] => ' . $env . '_pass\n}', $output);
    }
  }
}
