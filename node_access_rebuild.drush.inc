<?php
/**
 * @file
 * Provide Drush integration for node access rebuilding.
 */

/**
 * Implements hook_drush_help().
 */
function node_access_rebuild_drush_help($section) {
  switch ($section) {
    case 'drush:node-access-rebuild':
      return dt('Rebuild the node access table in a Drupal install.');
  }
}

/**
 * Implements hook_drush_command().
 */
function node_access_rebuild_drush_command() {
  $items = array();

  $items['node-access-rebuild'] = array(
    'description' => 'Rebuild the node access table in a Drupal install.',
    'callback' => 'drush_node_access_rebuild',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUSH, // No bootstrap.
    'examples' => array(
      'drush node-access-rebuild' => 'Rebuild the node access table.',
      'drush nar' => 'Use the alias to rebuild the node access table.',
    ),
    'aliases' => array('nar'),
  );

  return $items;
}

/**
 * Rebuild the registry.
 *
 * Before calling this we need to be bootstrapped to DRUPAL_BOOTSTRAP_DATABASE.
 */
function drush_node_access_rebuild() {
  define('MAINTENANCE_MODE', 'update');
  ini_set('memory_limit', -1);
  if (!drush_bootstrap_to_phase(DRUSH_BOOTSTRAP_DRUPAL_FULL)) {
    return drush_set_error('DRUPAL_SITE_NOT_FOUND', dt('You need to specify an alias or run this command within a drupal site.'));
  }
  $include_dir = DRUPAL_ROOT . '/includes';
  $module_dir = DRUPAL_ROOT . '/modules';
  // Use core directory if it exists.
  if (file_exists(DRUPAL_ROOT . '/core/includes/bootstrap.inc')) {
    $include_dir = DRUPAL_ROOT . '/core/includes';
    $module_dir = DRUPAL_ROOT . '/core/modules';
  }

  $includes = array(
    $include_dir . '/bootstrap.inc',
    $include_dir . '/common.inc',
    $include_dir . '/database.inc',
    $include_dir . '/schema.inc',
    $include_dir . '/actions.inc',
    $include_dir . '/entity.inc',
    $module_dir . '/system/system.module',
    $include_dir . '/database/database.inc',
    $include_dir . '/database/query.inc',
    $include_dir . '/database/select.inc',
    $include_dir . '/registry.inc',
    $include_dir . '/module.inc',
    $include_dir . '/menu.inc',
    $include_dir . '/file.inc',
    $include_dir . '/theme.inc',
    $include_dir . '/unicode.inc',
    $include_dir . '/locale.inc',
  );

  if (drush_drupal_major_version() == 7) {
    $cache_lock_path_absolute = variable_get('lock_inc');
    if (!empty($cache_lock_path_absolute)) {
      $cache_lock_path_relative = DRUPAL_ROOT . '/' . variable_get('lock_inc');
      // Ensure that the configured lock.inc really exists at that location and
      // is accessible. Otherwise we use the core lock.inc as fallback.
      if (is_readable($cache_lock_path_relative) && is_file($cache_lock_path_relative)) {
        $includes[] = $cache_lock_path_relative;
        drush_log(dt("We will use relative variant of lock.inc: @lock", array('@lock' => $cache_lock_path_relative)));
      }
      elseif (is_readable($cache_lock_path_absolute) && is_file($cache_lock_path_absolute)) {
        $includes[] = $cache_lock_path_absolute;
        drush_log(dt("We will use absolute variant of lock.inc: @lock", array('@lock' => $cache_lock_path_absolute)));
      }
      else {
        drush_log(dt('We will use core implementation of lock.inc as fallback.'));
        $includes[] = DRUPAL_ROOT . '/includes/lock.inc';
      }
    }
    else {
      $includes[] = DRUPAL_ROOT . '/includes/lock.inc';
    }
  }
  elseif (drush_drupal_major_version() > 7) {
    // TODO
    // http://api.drupal.org/api/drupal/namespace/Drupal!Core!Lock/8
    $includes[] = $module_dir . '/entity/entity.module';
    $includes[] = $module_dir . '/entity/entity.controller.inc';
  }
  // In Drupal 6 the configured lock.inc is already loaded during
  // DRUSH_BOOTSTRAP_DRUPAL_DATABASE.

  foreach ($includes as $include) {
    if (file_exists($include)) {
      require_once $include;
    }
  }

  if (DRUSH_MAJOR_VERSION > 4) {
    drush_cache_clear_drush();
    drush_log(dt('Internal Drush cache cleared with drush_cache_clear_drush (1).'));
  }

  if (!function_exists('module_list')) {
    drush_log(dt('ERROR! Node Access Rebuild requires a working Drupal site to operate on.'), 'warning');
    drush_log(dt('Please either cd to a directory containing a Drupal settings.php file,'), 'warning');
    drush_log(dt('or use a working site @alias outside of Drupal root directory tree.'), 'warning');
    exit;
  }

  // This is where the magic starts. This is largely based on the code in
  // node_access_rebuild().
  db_delete('node_access')->execute();
  // Only recalculate if the site is using a node_access module.
  if (count(module_implements('node_grants'))) {
    // Rebuild newest nodes first so that recent content becomes available
    // quickly.
    $nids = db_query("SELECT nid FROM {node} ORDER BY nid DESC")->fetchCol();
    $total = count($nids);
    $count = 0;
    foreach ($nids as $nid) {
      $node = node_load($nid, NULL, TRUE);
      // To preserve database integrity, only acquire grants if the node
      // loads successfully.
      if (!empty($node)) {
        node_access_acquire_grants($node);
      }
      $count++;

      // Report progress for every 10 nodes.
      if ($count % 500 == 0) {
        $percentage = round($count / $total * 100, 1);
        drush_log(
          dt(
            'Rebuilt node access for @count/@total nodes (@percentage%).',
            array(
              '@count' => $count,
              '@total' => $total,
              '@percentage' => $percentage,
            )
          ),
          'ok');
      }
    }
  }
  else {
    // Not using any node_access modules. Add the default grant.
    db_insert('node_access')
      ->fields(array(
        'nid' => 0,
        'realm' => 'all',
        'gid' => 0,
        'grant_view' => 1,
        'grant_update' => 0,
        'grant_delete' => 0,
      ))
      ->execute();
  }

  if (!isset($batch)) {
    drush_log(dt('Content permissions have been rebuilt.'), 'ok');
    node_access_needs_rebuild(FALSE);
    cache_clear_all();
  }

}
