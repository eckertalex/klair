vim.opt.makeprg = "./conformance/build.sh"

vim.opt.errorformat = {
	"%f(%l:%c) Error: %m",
	"%f(%l:%c) Warning: %m",
	"%-G%.%#",
}
