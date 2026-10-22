  const createFeatureNode = (node, depth = 0, index = 0) => {
    if (!node.children?.length) {
      const leaf = document.createElement('div');
      leaf.className = 'config-tree-row config-tree-leaf';
      leaf.setAttribute('role', 'treeitem');
      const label = document.createElement('span');
      label.className = 'config-tree-label';
      label.textContent = formatTreeLabel(node.key || node.title);
      leaf.append(label);
      return leaf;
    }

    //test for now wait for website release
    //LOADUCK ON MIT LICENSE
    //LOADUCK WEBSITE LICENSES.
    //all restricted.
