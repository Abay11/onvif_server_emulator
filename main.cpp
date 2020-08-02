#include <iostream>
#include <cstdlib>

#include <http_parser/http_parser.h>
#include <boost/asio.hpp>

int main()
{
	using namespace std;

	http_parser_settings settings;

	http_parser *parser = (http_parser*)malloc(sizeof(http_parser));
	http_parser_init(parser, HTTP_REQUEST);

	cout << "OK" << endl;
}