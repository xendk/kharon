<?php

/**
 * @file
 * Tests the fetch command.
 */
include_once 'Kharon_CommandTestCase.php';

class KharonGetCommandCase extends Kharon_CommandTestCase {

  /**
   * Get a variable from a test site.
   *
   * This is more problematic than you should think.
   */
  function getVariable($var, $env = NULL) {
    $this->drush('variable-get', array($var), array('pipe' => TRUE), $env);
    eval($this->getOutput());
    return $variables[$var];
}
  /**
   * Test that init works.
   */
  function testBasic() {
    $options = $this->kharonInit();
    $kharon_dir = $options['kharon'];

    // foreach ($this->sites as $env => $def) {
    // Not testing on D8 just yet.
    foreach (array(6, 7) as $major) {
      if ($major == 6) {
        $public_file_path_var = 'file_directory_path';
      }
      else {
        $public_file_path_var = 'file_public_path';
      }
      $this->log('Testing ' . $major . '.x.');
      $this->resetDrupal();
      $this->setUpDrupal(1, TRUE, $major);
      $env = reset(array_keys($this->sites));
      $def = reset($this->sites);
      $import_name = 'bia-' . $env . $major;
      $site_dir = $this->webroot() . '/sites/' . $env;
      // Create a file to sync.
      touch($site_dir . '/files/public_file');

      // Touch the DB.
      $this->drush('variable-set 2>&1', array('test_value', 'eros'), array(), '@' . $env);

      $this->drush('kharon-register', array($import_name, $this->webroot(), $env), $options);

      $this->drush('kharon-fetch', array($import_name), $options);

      // Create another file to sync.
      touch($site_dir . '/files/public_file2');

      // Touch the DB again.
      $this->drush('variable-set 2>&1', array('test_value2', 'eris'), array(), '@' . $env);

      $this->drush('kharon-fetch', array($import_name), $options);

      // Find dumps in the site.
      $dumps = array();
      $this->drush('kharon-list 2>&1', array($import_name), $options);
      $output = $this->getOutput();
      foreach ($this->getOutputAsList() as $line) {
        if ($dump = trim($line)) {
          $dumps[] = $dump;
        }
      }

      $this->assertCount(2, $dumps);
      sort($dumps);
      list($first_dump, $second_dump) = $dumps;

      $this->resetDrupal();
      $this->setUpDrupal(1, TRUE, $major);

      // Check that it properly errors an unknown site.
      $this->drush('kharon-get 2>&1', array('zelus'), $options, '@' . $env, NULL, self::EXIT_ERROR);
      $this->assertRegexp('/No such site/', $this->getOutput());

      // But it should work on with the right one.
      $this->drush('kharon-get', array($import_name), $options, '@' . $env);

      // Check that the variables exists after importing,
      $this->drush('variable-get 2>&1', array('test_value'), array(), '@' . $env);
      $this->assertRegexp('/eros/', $this->getOutput());

      // Second var, only in the latest dump.
      $this->drush('variable-get 2>&1', array('test_value2'), array(), '@' . $env);
      $this->assertRegexp('/eris/', $this->getOutput());

      // Check that the files exists after importing.
      // Get the configured path.
      $path = $this->getVariable($public_file_path_var, '@' . $env);
      $this->assertFileExists($this->webroot() . '/' . $path . '/public_file');
      $this->assertFileExists($this->webroot() . '/' . $path . '/public_file2');

      $this->resetDrupal();
      $this->setUpDrupal(1, TRUE, $major);

      // Asking for an non-existent dump should fail.
      $this->drush('kharon-get 2>&1', array($import_name, '123'), $options, '@' . $env, NULL, self::EXIT_ERROR);
      $this->assertRegexp('/No such dump/', $this->getOutput());

      // Asking for the first dump should also work.
      $this->drush('kharon-get', array($import_name, $first_dump), $options, '@' . $env);

      // Check that the variables exists after importing,
      $this->drush('variable-get 2>&1', array('test_value'), array(), '@' . $env);
      $this->assertRegexp('/eros/', $this->getOutput());

      // But not the second var.
      $this->drush('variable-get 2>&1', array('test_value2'), array(), '@' . $env, NULL, self::EXIT_ERROR);
      $this->assertRegexp('/No matching variable found./', $this->getOutput());

      // Check that the files exists after importing.
      // Get the configured path.
      $path = $this->getVariable($public_file_path_var, '@' . $env);
      $this->assertFileExists($this->webroot() . '/' . $path . '/public_file');
      // But this shouldn't.
      $this->assertFileNotExists($this->webroot() . '/' . $path . '/public_file2');
    }
  }
}
