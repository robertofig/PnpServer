async function PrimeMethod() {
	let primeUri = 'prime?number=' + document.getElementById('number').value;
	
	fetch(primeUri, {
		method: 'GET'
	})
	.then((response) => response.json())
	.then((result) => {
		let primeResponse = "";
		if (result.err == "prime") {
			primeResponse = "Input number is a prime!";
		} else if (result.err == "not-prime") {
			primeResponse = "Not a prime; closest prime is " + result.ret + ".";
		} else if (result.err == "too-big") {
			primeResponse = "Error: input number too big (max 1000000).";
		} else {
			primeResponse = "Error: input number is negative."
		};
		document.getElementById("show-prime").innerHTML = primeResponse;
	});
};

async function ModifyMethod() {
	let formElement = document.querySelector("form");
	let postData = new FormData(formElement);
	
	let req = new XMLHttpRequest();
	req.onreadystatechange = async function() {
		let errModify = document.getElementById("error-modify");
		let retImage = document.getElementById("show-modify");
		
		if (this.readyState == 4 && this.status == 200) {
			const urlCreator = window.URL || window.webkitURL;
			retImage.src = urlCreator.createObjectURL(this.response);
			errModify.innerHTML = "";
		} else if (this.readyState == 4 && this.status == 406) {
			let errModify = document.getElementById("error-modify");
			retImage.src = "";
			errModify.innerHTML = "Error: File type incorrect (must be .jpg, .png, or .bmp)";
		} else if (this.readyState == 4 && this.status == 413) {
			retImage.src = "";
			errModify.innerHTML = "Error: File too big (must be < 10MB)";
		};
	};
	req.open("POST", "modify", true);
	req.responseType = "blob";
	req.send(postData);
};