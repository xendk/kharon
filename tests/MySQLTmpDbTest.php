<?php

/**
 * @file
 * Tests the MySOLTmpDb class.
 */

include_once dirname(dirname(__FILE__)) . '/kharon.mysql.inc';

class MySOLTmpDbCase extends Drush_UnitTestCase {
  /**
   * Basic testing of the class.
   */
  function testBasic() {
    if (!preg_match('{mysql://(?P<user>[^:]+):(?P<pass>[^@]*)@(?P<host>.*)}', UNISH_DB_URL, $db_settings)) {
      $this->fail('Could not parse db credentials from UNISH_DB_URL.');
    }
    $db_settings['prefix'] = 'unish_kharon_test_';

    // Our own connection to the db.
    $db = new PDO('mysql:host=' . $db_settings['host'] . ';charset=utf8', $db_settings['user'], $db_settings['pass']);

    // Create a temporary database.
    $tmp_db = new MySQLTmpDb($db_settings, 'test');

    // Check that the database exists.
    $res = $db->query("SHOW DATABASES LIKE '" . $tmp_db->name() . "'")->fetchAll();
    $this->assertEquals(1, count($res));

    // Switch to the db.
    $db->query("USE " . $tmp_db->name());

    // Import a simple database dump.
    $tmp_db->import(dirname(__FILE__) . '/fixtures/test_db.sql');

    $res = $db->query("SHOW TABLES")->fetchAll(PDO::FETCH_COLUMN);
    $this->assertEquals(array('one_table'), $res);

    $dump_file = tempnam('/tmp', 'kharon_test_');
    // Dump the database to a file.
    $tmp_db->dump($dump_file);

    // Simplistic sanity check to see if it looks like the db was really dumped.
    $dump = file_get_contents($dump_file);
    unlink($dump_file);
    $this->assertRegExp('/one_table/', $dump);
    $this->assertRegExp('/one_column/', $dump);

    // Clean up (delete database).
    $tmp_db->cleanup();

    // Check that the database was properly removed.
    $res = $db->query("SHOW DATABASES LIKE '" . $tmp_db->name() . "'")->fetchAll();
    $this->assertEquals(0, count($res));
  }
}
