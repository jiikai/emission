function onDOMready() {
	const selectType  = document.getElementById('choice-chart-type'),
        selectYear  = document.getElementById('select-year'),
        selectRange = document.getElementById('select-range'),
        selectCntr  = document.getElementById('select-country'),
        srchCntr    = document.getElementById('search-countries'),
        cntrList    = document.getElementById('cntr-list'),
        cntrCount   = document.getElementById('cntr-count'),
        spanRange   = document.getElementById('span-data-range');

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
      spanRange.style.display = 'inherit';
      selectRange.style.display = 'inline-flex';
      selectRange.addEventListener('input', () => {
        if (fromYear.selectedIndex >= toYear.selectedIndex)
          toYear.selectedIndex = fromYear.selectedIndex;
      });
      srchCntr.style.display = 'inherit';
      cntrList.style.display = 'inherit';
    } else {
      spanRange.style.display = 'none';
      selectRange.style.display = 'none';
      fromYear.required = false;
      toYear.required   = false;
      srchCntr.style.display = 'none';
      cntrList.style.display = 'none';

      for (let i = 1980; i <= 2014; ++i)
        choiceYear.insertAdjacentHTML('beforeend',
            '<option id="f' + i + 'y" value="' + i + '">' + i + '</option>');
      choiceYear.required 			= true;
      selectYear.style.display  = 'block';
    }
  });

  const cntrArr = Array.from(selectCntr.children);
  document.getElementById('cbox-mod').addEventListener('input', event => {
  	const elem 					= event.target;
    const checkedState  = elem.checked,
    			cntrClass 		= 'opt-cntr-type-' + elem.id.charAt(0);
    cntrArr.filter(elem => elem.className === cntrClass).forEach(elem => {
        elem.disabled = !checkedState;
    });
  });

	const srchElem = document.getElementById('cntr-srch');
  srchElem.addEventListener('input', () => {
    let srchStr = srchElem.value.toLowerCase();
    if (!srchStr.length)
      cntrArr.forEach(elem => elem.style.display = '');
    else
      cntrArr.forEach(elem => {
        if (elem.value.toLowerCase().indexOf(srchStr) === -1)
          elem.style.display = 'none';
        else
          elem.style.display = '';
      });
    selectCntr.selectedIndex = cntrArr.findIndex(elem =>
                                  elem.style.display !== 'none');
  });

	let cntrListItems = [cntrArr.length];
  cntrListItems.fill(0);
	const cntrAddBtn  = document.getElementById('o-add-btn');
  cntrAddBtn.addEventListener('click', event => {
    let selectedIndex   = selectCntr.selectedIndex;
    const option        = selectCntr.options[selectedIndex];
    option.disabled     = true;
    if (cntrListItems[selectedIndex])
      cntrListItems[selectedIndex].style.display = '';
    else {
      const name = option.value,
       			id 	 = option.id;
      const cbox = '<input name="ccode" type="checkbox" value="'+id+'"checked>',
            icon = '<i class="fas fa-times-circle"></i>';
      cntrCount.value = parseInt(cntrCount.value) + 1;
      cntrList.insertAdjacentHTML('beforeend',
      		'<label id=lbl' + id + ' class="country-list-item">'+ cbox + icon
          + name + '</label>');
      let newItem = cntrList.lastChild;
      cntrListItems[selectedIndex] = newItem;
      newItem.addEventListener('click', event => {
        cntrCount.value = parseInt(cntrCount.value) - 1;
        document.getElementById(id).disabled = false;
        newItem.style.display = 'none';
        event.stopPropagation();
      });
    }
    selectCntr.selectedIndex = cntrArr.findIndex(elem => !elem.disabled
                                  && elem.style.display !== 'none');
    cntrAddBtn.blur();
    srchElem.focus();
    event.stopPropagation();
  });
}

if (document.readyState != 'loading')
	onDOMready();
else
	document.addEventListener('DOMContentLoaded', onDOMready);
