<?php

/**
 * @file
 * Tests the fetch command.
 */
include_once 'Kharon_CommandTestCase.php';

class KharonFetchCommandCase extends Kharon_CommandTestCase {
  public function setUp() {
    $this->setUpDrupal(3);
  }

  /**
   * Test that init works.
   */
  function testBasic() {
    $options = $this->kharonInit();
    $kharon_dir = $options['kharon'];

    foreach ($this->sites as $env => $def) {
      $import_name = 'aite-' . $env;
      $this->drush('kharon-register 2>&1', array($import_name, $this->webroot(), $env), $options);
      $this->drush('kharon-fetch', array($import_name), $options);
      $this->assertFileExists($kharon_dir . '/' . $import_name);
      // @todo get a listing of dirs in the folder, check that that there's only
      //   one and that it contains an info file. Check for dump, dump size,
      //   public folder.
    }
  }
}
