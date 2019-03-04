function onDOMready() {

	const selectType  = document.getElementById('choice-chart-type'),
        selectYear  = document.getElementById('select-year'),
        selectRange = document.getElementById('select-range'),
        selectCntr  = document.getElementById('select-country'),
        srchCntr    = document.getElementById('search-countries'),
        cntrList    = document.getElementById('cntr-list'),
        cntrCount   = document.getElementById('cntr-count');

	selectType.addEventListener('input', event => {
    const fromYear    = document.getElementById('from-year'),
          toYear      = document.getElementById('to-year'),
          choiceYear  = document.getElementById('choice-year');
    if (selectType.value === 'line') {
      selectYear.style.display	= 'none';
      choiceYear.required 		  = false;
      for (let i = 1980; i < 2013; ++i) {
        fromYear.insertAdjacentHTML('beforeend',
        		'<option id="f' + i + 'y" value="' + i + '">' + i + '</option>');
        let j = i + 1;
        toYear.insertAdjacentHTML('beforeend',
        		'<option id="t' + j + 'y"value="' + j + '">' + j + '</option>');
      }
      fromYear.required = true;
      toYear.required   = true;
      document.getElementById('span-data-range').style.display = 'inherit';
      selectRange.style.display = 'inline-flex';
      selectRange.addEventListener('input', () => {
        if (fromYear.selectedIndex >= toYear.selectedIndex)
          toYear.selectedIndex = fromYear.selectedIndex;
      });
      srchCntr.style.display = 'inherit';
      cntrList.style.display = 'inherit';
    } else {
      document.getElementById('span-data-range').style.display = 'none';
      selectRange.style.display = 'none';
      fromYear.required = false;
      toYear.required   = false;
      srchCntr.style.display = 'none';
      cntrList.style.display = 'none';

      const choiceYear = document.getElementById('choice-year');
      for (let i = 1980; i <= 2014; ++i)
        choiceYear.insertAdjacentHTML('beforeend',
            '<option id="f' + i + 'y" value="' + i + '">' + i + '</option>');

      choiceYear.required 			= true;
      selectYear.style.display  = 'block';
    }
  });

  const cntrArr = selectCntr.children;
	const ncountries = cntrArr.length;

  document.getElementById('cbox-mod').addEventListener('input', event => {
  	const elem 					= event.target;
    const checkedState  = elem.checked,
    			cntrClass 		= 'opt-cntr-type-' + elem.id.charAt(0);
    for (let i = 0; i < ncountries; ++i)
    	if (cntrArr[i].className === cntrClass)
      	cntrArr[i].disabled = !checkedState;
  });

	const srchElem = document.getElementById('cntr-srch');
  let prevSelected = 0;
  srchElem.addEventListener('input', () => {
    let srchStr = srchElem.value.toLowerCase();
    if (!srchStr.length) {
    	for (let i = 0; i < ncountries; ++i)
      	cntrArr[i].style.display = '';
    	cntrArr[0].parentElement.selectedIndex = 0;
    } else {
      let selected = false;
      for (let i = 0; i < ncountries; ++i) {
        let elem = cntrArr[i];
        if (!elem.disabled) {
          if (elem.value.toLowerCase().indexOf(srchStr) === -1)
            elem.style.display = 'none';
          else {
            elem.style.display = '';
            if (!selected) {
              elem.parentElement.selectedIndex = i;
              selected = true;
            }
          }
        }
      }
    }
  });

	let cntrListItems = [ncountries];
	const cntrAddBtn = document.getElementById('o-add-btn');
  cntrAddBtn.addEventListener('click', event => {
    const option  = selectCntr.options[selectCntr.selectedIndex];
    const name  	= option.value,
     			id 			= option.id;
    option.disabled = true;
    const cbox 		= '<input name="ccode" type="checkbox" value="' + id + '" checked>',
     			icon 		= '<i class="fas fa-times-circle"></i>';
    cntrCount.value = parseInt(cntrCount.value) + 1;
    cntrList.insertAdjacentHTML('beforeend',
    		'<label id=lbl' + id + ' class="country-list-item">' + cbox + icon + name + '</label>');
    selectCntr.selectedIndex = -1;
    let newItem = cntrList.lastChild;
    newItem.addEventListener('click', event => {
      cntrCount.value = parseInt(cntrCount.value) - 1;
      document.getElementById(id).disabled = false;
      while (newItem.firstChild) {
  			newItem.removeChild(newItem.firstChild);
			}
      newItem.parentElement.removeChild(newItem);
      event.stopPropagation();
    });
    cntrAddBtn.blur();
    srchElem.parentElement.click();
    event.stopPropagation();
  });

}

if (document.readyState != 'loading')
	onDOMready();
else
	document.addEventListener('DOMContentLoaded', onDOMready);
