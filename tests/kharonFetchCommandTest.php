<?php

/**
 * @file
 * Tests the fetch command.
 */
include_once 'Kharon_CommandTestCase.php';

class KharonFetchCommandCase extends Kharon_CommandTestCase {
  /**
   * Test that init works.
   */
  function testBasic() {
    $options = $this->kharonInit();
    $kharon_dir = $options['kharon'];

    // foreach ($this->sites as $env => $def) {
    // Not testing on D8 just yet.
    foreach (array(6, 7) as $major) {
      $this->log('Testing ' . $major . '.x.');
      $this->resetDrupal();
      $this->setUpDrupal(1, TRUE, $major);
      $env = reset(array_keys($this->sites));
      $def = reset($this->sites);
      $import_name = 'aite-' . $env . $major;
      $site_dir = $this->webroot() . '/sites/' . $env;
      // Create a file to sync.
      touch($site_dir . '/files/public_file');
      $this->drush('kharon-register 2>&1', array($import_name, $this->webroot(), $env), $options);
      $this->drush('kharon-fetch 2>&1', array($import_name), $options);
      $this->assertFileExists($kharon_dir . '/' . $import_name);

      // Find dumps in the site.
      $dumps = array();
      $dir = dir($kharon_dir . '/' . $import_name);
      while (FALSE !== ($entry = $dir->read())) {
        // Ignore anything starting with a dot, and the info file.
        if (!preg_match('/^(\..*|config)$/', $entry)) {
          $dumps[] = $entry;
        }
      }
      $dir->close();

      $this->assertCount(1, $dumps);
      $dump_name = reset($dumps);

      // There should be an info file.
      $this->assertFileExists($kharon_dir . '/' . $import_name . '/' . $dump_name . '/info');

      // And a SQL file.
      $this->assertFileExists($kharon_dir . '/' . $import_name . '/' . $dump_name . '/database.sql.gz');
      // With a non-zero size.
      $this->assertGreaterThan(0, filesize($kharon_dir . '/' . $import_name . '/' . $dump_name . '/database.sql.gz'));

      // Check that there's a folder for public files.
      $this->assertFileExists($kharon_dir . '/' . $import_name . '/' . $dump_name . '/public');
      // And our test file.
      $this->assertFileExists($kharon_dir . '/' . $import_name . '/' . $dump_name . '/public/public_file');

      // Cuurently not testable as the default install doesn't configure a
      // private directory (for good reasons). We'll need to set it up ourselves
      // if we want to test this.
      // if ($def['core'] == '7.x' || $def['core'] == '8.x') {
      //   $this->assertFileExists($kharon_dir . '/' . $import_name . '/' . $dump_name . '/private');
      // }
    }
  }
}
