<?php

/**
 * @file
 * Tests the fetch command.
 */
include_once 'Kharon_CommandTestCase.php';

class KharonListCommandCase extends Kharon_CommandTestCase {
  /**
   * Test that init works.
   */
  function testBasic() {
    $options = $this->kharonInit();
    $kharon_dir = $options['kharon'];

    // Not testing on D8 just yet.
    foreach (array(6, 7) as $major) {
      $this->log('Testing ' . $major . '.x.');
      $this->resetDrupal();
      $this->setUpDrupal(1, TRUE, $major);
      $env = reset(array_keys($this->sites));
      $def = reset($this->sites);
      $import_name = 'bia-' . $env . $major;
      $this->drush('kharon-register', array($import_name, $this->webroot(), $env), $options);

      // Create a dump.
      $this->drush('kharon-fetch', array($import_name), $options);

      // And another one.
      $this->drush('kharon-fetch', array($import_name), $options);

      // Get a site list.
      $this->drush('kharon-list', array(), $options);
      $this->assertRegexp('/' . $import_name . '/', $this->getOutput());

      // List dumps in site.
      $this->drush('kharon-list', array($import_name), $options);
      $dumps = array();
      // Filter out empty lines.
      $output = array_filter(explode("\n", $this->getOutput()));
      foreach ($output as $line) {
        $dumps[] = trim($line);
      }
      // Check for the right amount.
      $this->assertCount(2, $dumps);
      // Test that they seem to exist.
      foreach ($dumps as $dump) {
        $this->assertFileExists($kharon_dir . '/' . $import_name . '/' . $dump . '/info');
      }
    }
  }
}
