//package uk.co.srs.monotree;

/*
 * Copyright (C) 2005 Joel Crisp <jcrisp@s-r-s.co.uk>
 * Licensed under the MIT license:
 *    http://www.opensource.org/licenses/mit-license.html
 * I.e., do what you like, but keep copyright and there's NO WARRANTY.
 */

import java.io.File;
import java.io.InputStream;
import java.io.FileInputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.Reader;
import java.io.Writer;
import java.io.IOException;
import java.io.LineNumberReader;
import java.util.Map;
import java.util.HashMap;
import java.util.Properties;
import java.util.List;
import java.util.ArrayList;
import java.util.Arrays;
import net.sourceforge.gxl.GXLDocument;
import net.sourceforge.gxl.GXLGraph;
import net.sourceforge.gxl.GXLNode;
import net.sourceforge.gxl.GXLEdge;
import net.sourceforge.gxl.GXLString;
import net.sourceforge.gxl.GXLSet;
import net.sourceforge.gxl.GXLAttr;
import net.sourceforge.gxl.GXLInt;
import net.sourceforge.gxl.GXLTup;
import net.sourceforge.gxl.GXL;

/**
 * Simple filter to convert a monotone log output into a GXL graph
 *
 * NOTE: This file requires the GXL Java API from http://www.gupro.de/GXL/index.html
 * NOTE: Optionally you can get Graphviz from ATT and use the gxl2dot program to generate a dot file (input to graphviz)
 * NOTE: Required JDK 1.5 as it uses generics
 *
 * @author Joel Crisp
 */
public class Log2Gxl {

    /**
     * Main class. 
     * Invoke via: monotone --db=database.db log id | java -classpath gxl.jar:. uk.co.srs.monotree.Log2Gxl | gxl2dot >log.dot
     * or monotone --db=database.db log id | java -classpath gxl.jar:. uk.co.srs.monotree.Log2Gxl | gxl2dot | dot -Tsvg >log.svg
     * or monotone --db=database.db log id | java -classpath gxl.jar:. uk.co.srs.monotree.Log2Gxl --authorfile <file> | gxl2dot | dot -Tsvg >log.svg
     *
     * @param argv command line arguments, --authorfile <file> to specify a file mapping authors to colors
     */
    public static void main(String argv[]) throws IOException,IllegalStateException { 
	new Log2Gxl().Start(argv);
    }

    /**
     * Read a line from the std input stream and verify it starts with the specified prefix
     * 
     * @param prefix the prefix to check as a simple string
     * @return the line
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the prefix doesn't match the input line
     */
    private String readLine(String prefix) throws IOException,IllegalStateException {
	String line=source.readLine();
	if(line==null) throw new IOException(source.getLineNumber()+": Unexpected end of input");
	if(!line.startsWith(prefix)) throw new IllegalStateException(source.getLineNumber()+": Expected ["+prefix+"], got ["+line+"]");
        return line;
    }

    /**
     * Finalise a node by setting any deferred attributes
     * This is used to allow incremental construction of attributes during log entry parsing
     */
    private void commitNode() {
	currentNode.setAttr("style",new GXLString("filled"));
	if(tags.length()>0) currentNode.setAttr("shape",new GXLString("rect"));
	else currentNode.setAttr("shape",new GXLString("ellipse"));

	StringBuffer tooltip=new StringBuffer();
	tooltip.append(dates);
	tooltip.append("\\n"); // Escaped Newline escape sequence
	tooltip.append(authors);
	tooltip.append("\\n"); // Escaped Newline escape sequence
	tooltip.append(tags);
	currentNode.setAttr("tooltip",new GXLString(tooltip.toString()));

	// Blank the attributes for the next entry
	tags="";
	authors="";
	dates="";
	currentNode=null;
    }


    /**
     * Parse the header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */
    private void parseHeader() throws IOException,IllegalStateException {
	parseRevision();
	parseAncestors();
        parseAuthors();
	String line=lookahead();
	if(line.startsWith("Date:")) parseDates();
        parseBranches();
	line=lookahead();
	if(line.startsWith("Tag:")) parseTags();
    }

    /**
     * Parse the author(s) from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseAuthors() throws IOException,IllegalStateException {
	parseAuthor();
	String line=lookahead();
	if(line.startsWith("Author:")) parseAuthors();	
    }
    
    /**
     * Parse the tags(s) from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseTags() throws IOException,IllegalStateException {
	parseTag();
	String line=lookahead();
	if(line.startsWith("Tag:")) parseTags();	
    }

    /**
     * Parse the date(s) from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseDates() throws IOException,IllegalStateException {
	parseDate();
	String line=lookahead();
	if(line.startsWith("Date:")) parseDates();	
    }

    /**
     * Parse the branch(s) from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseBranches() throws IOException,IllegalStateException {
	parseBranch();
	String line=lookahead();
	if(line.startsWith("Branch:")) parseBranches();
    }

    /**
     * Parse a date line from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseDate() throws IOException,IllegalStateException {
	String line=readLine("Date:");
	String date=line.substring("Date:".length()+1);
	if(dates.length()==0) dates=date;
	else dates=dates+","+date;
    }

    /**
     * Parse a revision line from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseRevision() throws IOException,IllegalStateException {
	String line=readLine("Revision:");
	
	revision=line.substring("Revision:".length()+1);            
	currentNode=nodes.get(revision);
	if(currentNode==null) {
	    currentNode=createDefaultNode(revision);
	}
    }

    /**
     * Read a line marking the current place in the stream first and restoring it after reading the line
     * This method only supports lines up to 16384 characters in length. For monotone logs this should not be a problem
     * @throws IOException if there is a read error on the input stream 
     * @return the line which was read or null if the end of the input stream is reached
     */    
    private String lookahead() throws IOException {
        source.mark(16384);
        String line=source.readLine();
        source.reset();      
        return line;
    }
    
    /**
     * Parse the ancestors(s) from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected
     */    
    private void parseAncestors() throws IOException,IllegalStateException {
	parseAncestor();
        String line=lookahead();
        if(line.startsWith("Ancestor:")) parseAncestors();
    }

    /**
     * Create a GXL node with a few default attributes
     *
     * @param id the string identifier for the node (should be the revision hash id)
     * @return a new GXLNode with the specified id and some useful default attributes
     */
    private GXLNode createDefaultNode(String id) {
	GXLNode node=new GXLNode(id);
	// GXL Schema support. Commented out until I understand it a bit better
	// node.setType("MonotoneRevisionGraphSchema.gxl#RevisionNode");

	// These attributes are used by JGraphPad and DOT - node label - id truncated to 8 hex digits
	node.setAttr("Label",new GXLString(id.substring(0,8)));
	node.setAttr("label",new GXLString(id.substring(0,8)));

	// These attributes are used by DOT
	node.setAttr("URL",new GXLString("#"+id));

	// These attributes are used by JGraphPad - node border color
	node.setAttr("BorderColor",new GXLTup());
	GXLTup borderColor=(GXLTup)node.getAttr("BorderColor").getValue();
	borderColor.add(new GXLInt(0));
	borderColor.add(new GXLInt(0));
	borderColor.add(new GXLInt(0));

	// These attributes are used by JGraphPad - initial node location and size
	node.setAttr("Bounds",new GXLTup());
	GXLTup bounds=(GXLTup)node.getAttr("Bounds").getValue();
	bounds.add(new GXLInt(20));
	bounds.add(new GXLInt(20+nodes.size()*60));
	bounds.add(new GXLInt(20));
	bounds.add(new GXLInt(20));

	graph.add(node);
	nodes.put(id,node);
	return node;
    }

    /**
     * Parse an ancestor line from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseAncestor() throws IOException,IllegalStateException {
	String line=readLine("Ancestor:");
	String ancestor=line.substring("Ancestor:".length()+1);
	if(ancestor.length()!=0) {
	    GXLNode ancestorNode=nodes.get(ancestor);
	    if(ancestorNode==null) {
		ancestorNode=createDefaultNode(ancestor);
	    }
	    GXLEdge edge=new GXLEdge(ancestorNode,currentNode);
	    // GXL Schema support. Commented out until I understand it a bit better
            //edge.setType("MonotoneRevisionGraphSchema.gxl#AncestorEdge");
	    edge.setDirected(true);
	    graph.add(edge);
	}
    }

    /**
     * Add an entry to an attribute set on the current node.
     * Note that if the attribute does not exist it is created.
     * 
     * @param attrName the name of the attribute set
     * @param item the item to add to the attribute set
     */
    private void addToAttributeSet(String attrName,String item) {
	GXLAttr attrSet=currentNode.getAttr(attrName);
	if(attrSet==null) {
	    currentNode.setAttr(attrName,new GXLSet());
	    attrSet=currentNode.getAttr(attrName);
	}
	GXLSet value=(GXLSet)attrSet.getValue();
	value.add(new GXLString(item));	
    }
    
    /**
     * Parse an author line from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseAuthor() throws IOException,IllegalStateException {
        String line=readLine("Author:");
	String author=line.substring("Author:".length()+1);
	if(author.length()!=0) {
	    if(authors.length()==0) authors=author;
	    else authors=authors+","+author;

	    addToAttributeSet("Authors",author);

            if(colorAuthors) {
		String color=authorColorMap.get(author);
		if(color==null) {
		    color=colors[authorColorMap.size()];
		    authorColorMap.put(author,color);
		}
		currentNode.setAttr("fillcolor",new GXLString(color));
	    }
	}
    }

    /**
     * Parse a branch line from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseBranch() throws IOException,IllegalStateException {
	String line=readLine("Branch:");
	String branch=line.substring("Branch:".length()+1);
	if(branch.length()!=0) {
	    addToAttributeSet("Branches",branch);
	} 
    }

    /**
     * Parse a tag line from a header from a log entry
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseTag() throws IOException,IllegalStateException {
	String line=readLine("Tag:");
	String tag=line.substring("Tag:".length()+1);
	if(tag.length()!=0) {
	    if(tags.length()==0) tags=tag;
	    else tags=tags+","+tag;
	    addToAttributeSet("Tags",tag);
	} 
    }

    /**
     * Parse the deleted files section of a log entry from a monotone log output
     * Optionally pass the list of files to GXL (Don't do this is you're using dot as dxl2dot chokes on it)
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseDeletedFiles() throws IOException,IllegalStateException {
	String files=readFileBlock("Deleted files:");
	if(includeFiles) currentNode.setAttr("Deleted files",new GXLString(files));
    }

    /**
     * Parse the added files section of a log entry from a monotone log output
     * Optionally pass the list of files to GXL (Don't do this is you're using dot as dxl2dot chokes on it)
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseAddedFiles() throws IOException,IllegalStateException {
	String files=readFileBlock("Added files:");
	if(includeFiles) currentNode.setAttr("Added files",new GXLString(files));
    }

    /**
     * Parse a block of file names from one of the files sections of a log entry from a monotone log output
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     */
    private String readFileBlock(String header) throws IOException {
	String line=readLine(header);
	StringBuffer files=new StringBuffer();
	boolean parsing=true;
	while(parsing) {
	    line=lookahead();
	    if(!line.startsWith("        ")) { 
		parsing=false;
	    }
	    else {
		line=source.readLine();
		files.append(" ");
		files.append(line.substring(10));
	    }
	}
	return files.toString();
    }

    /**
     * Parse the modified files section of a log entry from a monotone log output
     * Optionally pass the list of files to GXL (Don't do this is you're using dot as dxl2dot chokes on it)
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseModifiedFiles() throws IOException,IllegalStateException {
	String files=readFileBlock("Modified files:");
	if(includeFiles) currentNode.setAttr("Modified files",new GXLString(files));
    }

    /**
     * Parse the change log section of a log entry from a monotone log output
     * Currently this is discarded
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseChangeLog() throws IOException,IllegalStateException {
        String line=source.readLine();
	if(line.length()>0) throw new IOException(source.getLineNumber()+": Unexpected data ["+line+"]");
	line=readLine("ChangeLog:");
        boolean parsing=true;
	while(parsing) {
	    line=lookahead();
	    if(line==null || line.startsWith("----")) {
		parsing=false;
	    }
	    else {
		line=source.readLine();
		// TODO: record changelog 
	    }
	}
    }

    /**
     * Parse the various files section of a log entry from a monotone log output
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void parseFiles() throws IOException,IllegalStateException {
        String line=source.readLine();
	if(line.length()>0) throw new IOException(source.getLineNumber()+": Unexpected data ["+line+"]");
        line=lookahead();
        if(line.startsWith("Deleted files:")) parseDeletedFiles();
        line=lookahead();
        if(line.startsWith("Added files:")) parseAddedFiles();
        line=lookahead(); 
	if(line.startsWith("Modified files:")) parseModifiedFiles();
    }

    /**
     * If true, set the background color of the node to indicate the author
     */
    private boolean colorAuthors=true;

    /**
     * Map of author identifiers to allocated colors
     */
    private Map<String,String> authorColorMap=new HashMap<String,String>();
    
    /**
     * Array of color names used to allocate colors
     * Don't make this static or final, it can get modified
     */
    private String[] colors=new String[] {
	"aliceblue",
	"antiquewhite",
	"antiquewhite1",
	"antiquewhite2",
	"antiquewhite3",
	"antiquewhite4",
	"aquamarine",
	"aquamarine1",
	"aquamarine2",
	"aquamarine3",
	"aquamarine4",
	"azure",
	"azure1",
	"azure2",
	"azure3",
	"azure4",
	"beige",
	"bisque",
	"bisque1",
	"bisque2",
	"bisque3",
	"bisque4",
	"black",
	"blanchedalmond",
	"blue",
	"blue1",
	"blue2",
	"blue3",
	"blue4",
	"blueviolet",
	"brown",
	"brown1",
	"brown2",
	"brown3",
	"brown4",
	"burlywood",
	"burlywood1",
	"burlywood2",
	"burlywood3",
	"burlywood4",
	"cadetblue",
	"cadetblue1",
	"cadetblue2",
	"cadetblue3",
	"cadetblue4",
	"chartreuse",
	"chartreuse1",
	"chartreuse2",
	"chartreuse3",
	"chartreuse4",
	"chocolate",
	"chocolate1",
	"chocolate2",
	"chocolate3",
	"chocolate4",
	"coral",
	"coral1",
	"coral2",
	"coral3",
	"coral4",
	"cornflowerblue",
	"cornsilk",
	"cornsilk1",
	"cornsilk2",
	"cornsilk3",
	"cornsilk4",
	"crimson",
	"cyan",
	"cyan1",
	"cyan2",
	"cyan3",
	"cyan4",
	"darkgoldenrod",
	"darkgoldenrod1",
	"darkgoldenrod2",
	"darkgoldenrod3",
	"darkgoldenrod4",
	"darkgreen",
	"darkkhaki",
	"darkolivegreen",
	"darkolivegreen1",
	"darkolivegreen2",
	"darkolivegreen3",
	"darkolivegreen4",
	"darkorange",
	"darkorange1",
	"darkorange2",
	"darkorange3",
	"darkorange4",
	"darkorchid",
	"darkorchid1",
	"darkorchid2",
	"darkorchid3",
	"darkorchid4",
	"darksalmon",
	"darkseagreen",
	"darkseagreen1",
	"darkseagreen2",
	"darkseagreen3",
	"darkseagreen4",
	"darkslateblue",
	"darkslategray",
	"darkslategray1",
	"darkslategray2",
	"darkslategray3",
	"darkslategray4",
	"darkslategrey",
	"darkturquoise",
	"darkviolet",
	"deeppink",
	"deeppink1",
	"deeppink2",
	"deeppink3",
	"deeppink4",
	"deepskyblue",
	"deepskyblue1",
	"deepskyblue2",
	"deepskyblue3",
	"deepskyblue4",
	"dimgray",
	"dimgrey",
	"dodgerblue",
	"dodgerblue1",
	"dodgerblue2",
	"dodgerblue3",
	"dodgerblue4",
	"firebrick",
	"firebrick1",
	"firebrick2",
	"firebrick3",
	"firebrick4",
	"floralwhite",
	"forestgreen",
	"gainsboro",
	"ghostwhite",
	"gold",
	"gold1",
	"gold2",
	"gold3",
	"gold4",
	"goldenrod",
	"goldenrod1",
	"goldenrod2",
	"goldenrod3",
	"goldenrod4",
	"gray",
	"gray0",
	"gray1",
	"gray10",
	"gray100",
	"gray11",
	"gray12",
	"gray13",
	"gray14",
	"gray15",
	"gray16",
	"gray17",
	"gray18",
	"gray19",
	"gray2",
	"gray20",
	"gray21",
	"gray22",
	"gray23",
	"gray24",
	"gray25",
	"gray26",
	"gray27",
	"gray28",
	"gray29",
	"gray3",
	"gray30",
	"gray31",
	"gray32",
	"gray33",
	"gray34",
	"gray35",
	"gray36",
	"gray37",
	"gray38",
	"gray39",
	"gray4",
	"gray40",
	"gray41",
	"gray42",
	"gray43",
	"gray44",
	"gray45",
	"gray46",
	"gray47",
	"gray48",
	"gray49",
	"gray5",
	"gray50",
	"gray51",
	"gray52",
	"gray53",
	"gray54",
	"gray55",
	"gray56",
	"gray57",
	"gray58",
	"gray59",
	"gray6",
	"gray60",
	"gray61",
	"gray62",
	"gray63",
	"gray64",
	"gray65",
	"gray66",
	"gray67",
	"gray68",
	"gray69",
	"gray7",
	"gray70",
	"gray71",
	"gray72",
	"gray73",
	"gray74",
	"gray75",
	"gray76",
	"gray77",
	"gray78",
	"gray79",
	"gray8",
	"gray80",
	"gray81",
	"gray82",
	"gray83",
	"gray84",
	"gray85",
	"gray86",
	"gray87",
	"gray88",
	"gray89",
	"gray9",
	"gray90",
	"gray91",
	"gray92",
	"gray93",
	"gray94",
	"gray95",
	"gray96",
	"gray97",
	"gray98",
	"gray99",
	"green",
	"green1",
	"green2",
	"green3",
	"green4",
	"greenyellow",
	"grey",
	"grey0",
	"grey1",
	"grey10",
	"grey100",
	"grey11",
	"grey12",
	"grey13",
	"grey14",
	"grey15",
	"grey16",
	"grey17",
	"grey18",
	"grey19",
	"grey2",
	"grey20",
	"grey21",
	"grey22",
	"grey23",
	"grey24",
	"grey25",
	"grey26",
	"grey27",
	"grey28",
	"grey29",
	"grey3",
	"grey30",
	"grey31",
	"grey32",
	"grey33",
	"grey34",
	"grey35",
	"grey36",
	"grey37",
	"grey38",
	"grey39",
	"grey4",
	"grey40",
	"grey41",
	"grey42",
	"grey43",
	"grey44",
	"grey45",
	"grey46",
	"grey47",
	"grey48",
	"grey49",
	"grey5",
	"grey50",
	"grey51",
	"grey52",
	"grey53",
	"grey54",
	"grey55",
	"grey56",
	"grey57",
	"grey58",
	"grey59",
	"grey6",
	"grey60",
	"grey61",
	"grey62",
	"grey63",
	"grey64",
	"grey65",
	"grey66",
	"grey6",
	"grey68",
	"grey69",
	"grey7",
	"grey70",
	"grey7",
	"grey72",
	"grey73",
	"grey74",
	"grey75",
	"grey7",
	"grey77",
	"grey78",
	"grey79",
	"grey8",
	"grey8",
	"grey81",
	"grey82",
	"grey83",
	"grey84",
	"grey8",
	"grey86",
	"grey87",
	"grey88",
	"grey89",
	"grey",
	"grey90",
	"grey91",
	"grey92",
	"grey93",
	"grey9",
	"grey95",
	"grey96",
	"grey97",
	"grey98",
	"grey9",
	"honeydew",
	"honeydew1",
	"honeydew2",
	"honeydew3",
	"honeydew",
	"hotpink",
	"hotpink1",
	"hotpink2",
	"hotpink3",
	"hotpink",
	"indianred",
	"indianred1",
	"indianred2",
	"indianred3",
	"indianred",
	"indigo",
	"ivory",
	"ivory1",
	"ivory2",
	"ivory",
	"ivory4",
	"khaki",
	"khaki1",
	"khaki2",
	"khaki",
	"khaki4",
	"lavender",
	"lavenderblush",
	"lavenderblush1",
	"lavenderblush",
	"lavenderblush3",
	"lavenderblush4",
	"lawngreen",
	"lemonchiffon",
	"lemonchiffon",
	"lemonchiffon2",
	"lemonchiffon3",
	"lemonchiffon4",
	"lightblue",
	"lightblue",
	"lightblue2",
	"lightblue3",
	"lightblue4",
	"lightcoral",
	"lightcya",
	"lightcyan1",
	"lightcyan2",
	"lightcyan3",
	"lightcyan4",
	"lightgoldenro",
	"lightgoldenrod1",
	"lightgoldenrod2",
	"lightgoldenrod3",
	"lightgoldenrod4",
	"lightgoldenrodyello",
	"lightgray",
	"lightgrey",
	"lightpink",
	"lightpink1",
	"lightpink",
	"lightpink3",
	"lightpink4",
	"lightsalmon",
	"lightsalmon1",
	"lightsalmon",
	"lightsalmon3",
	"lightsalmon4",
	"lightseagreen",
	"lightskyblue",
	"lightskyblue",
	"lightskyblue2",
	"lightskyblue3",
	"lightskyblue4",
	"lightslateblue",
	"lightslategra",
	"lightslategrey",
	"lightsteelblue",
	"lightsteelblue1",
	"lightsteelblue2",
	"lightsteelblue",
	"lightsteelblue4",
	"lightyellow",
	"lightyellow1",
	"lightyellow2",
	"lightyellow",
	"lightyellow4",
	"limegreen",
	"linen",
	"magenta",
	"magenta",
	"magenta2",
	"magenta3",
	"magenta4",
	"maroon",
	"maroon",
	"maroon2",
	"maroon3",
	"maroon4",
	"mediumaquamarine",
	"mediumblu",
	"mediumorchid",
	"mediumorchid1",
	"mediumorchid2",
	"mediumorchid3",
	"mediumorchid",
	"mediumpurple",
	"mediumpurple1",
	"mediumpurple2",
	"mediumpurple3",
	"mediumpurple",
	"mediumseagreen",
	"mediumslateblue",
	"mediumspringgreen",
	"mediumturquoise",
	"mediumvioletre",
	"midnightblue",
	"mintcream",
	"mistyrose",
	"mistyrose1",
	"mistyrose",
	"mistyrose3",
	"mistyrose4",
	"moccasin",
	"navajowhite",
	"navajowhite",
	"navajowhite2",
	"navajowhite3",
	"navajowhite4",
	"navy",
	"navyblu",
	"oldlace",
	"olivedrab",
	"olivedrab1",
	"olivedrab2",
	"olivedrab",
	"olivedrab4",
	"orange",
	"orange1",
	"orange2",
	"orange",
	"orange4",
	"orangered",
	"orangered1",
	"orangered2",
	"orangered",
	"orangered4",
	"orchid",
	"orchid1",
	"orchid2",
	"orchid",
	"orchid4",
	"palegoldenrod",
	"palegreen",
	"palegreen1",
	"palegreen",
	"palegreen3",
	"palegreen4",
	"paleturquoise",
	"paleturquoise1",
	"paleturquoise",
	"paleturquoise3",
	"paleturquoise4",
	"palevioletred",
	"palevioletred1",
	"palevioletred",
	"palevioletred3",
	"palevioletred4",
	"papayawhip",
	"peachpuff",
	"peachpuff",
	"peachpuff2",
	"peachpuff3",
	"peachpuff4",
	"peru",
	"pin",
	"pink1",
	"pink2",
	"pink3",
	"pink4",
	"plu",
	"plum1",
	"plum2",
	"plum3",
	"plum4",
	"powderblu",
	"purple",
	"purple1",
	"purple2",
	"purple3",
	"purple",
	"red",
	"red1",
	"red2",
	"red3",
	"red",
	"rosybrown",
	"rosybrown1",
	"rosybrown2",
	"rosybrown3",
	"rosybrown",
	"royalblue",
	"royalblue1",
	"royalblue2",
	"royalblue3",
	"royalblue",
	"saddlebrown",
	"salmon",
	"salmon1",
	"salmon2",
	"salmon",
	"salmon4",
	"sandybrown",
	"seagreen",
	"seagreen1",
	"seagreen",
	"seagreen3",
	"seagreen4",
	"seashell",
	"seashell1",
	"seashell",
	"seashell3",
	"seashell4",
	"sienna",
	"sienna1",
	"sienna",
	"sienna3",
	"sienna4",
	"skyblue",
	"skyblue1",
	"skyblue",
	"skyblue3",
	"skyblue4",
	"slateblue",
	"slateblue1",
	"slateblue",
	"slateblue3",
	"slateblue4",
	"slategray",
	"slategray1",
	"slategray",
	"slategray3",
	"slategray4",
	"slategrey",
	"snow",
	"snow",
	"snow2",
	"snow3",
	"snow4",
	"springgreen",
	"springgreen",
	"springgreen2",
	"springgreen3",
	"springgreen4",
	"steelblue",
	"steelblue",
	"steelblue2",
	"steelblue3",
	"steelblue4",
	"tan",
	"tan",
	"tan2",
	"tan3",
	"tan4",
	"thistle",
	"thistle",
	"thistle2",
	"thistle3",
	"thistle4",
	"tomato",
	"tomato",
	"tomato2",
	"tomato3",
	"tomato4",
	"transparent",
	"turquois",
	"turquoise1",
	"turquoise2",
	"turquoise3",
	"turquoise4",
	"viole",
	"violetred",
	"violetred1",
	"violetred2",
	"violetred3",
	"violetred",
	"wheat",
	"wheat1",
	"wheat2",
	"wheat3",
	"wheat",
	"white",
	"whitesmoke",
	"yellow",
	"yellow1",
	"yellow",
	"yellow3",
	"yellow4",
	"yellowgreen"
    };

    /**
     * Deferred attribute for date
     */
    private String dates="";

    /**
     * Deferred attribute for author
     */
    private String authors="";

    /**
     * Deferred attribute for tags
     */
    private String tags="";

    /**
     * Log output line source which is a wrapper on System.in
     */
    private LineNumberReader source;

    /**
     * The GXL Graph which is being built
     */
    private GXLGraph graph;

    /**
     * A cache of nodes which we have already seen
     */
    private Map<String,GXLNode> nodes;
    
    /**
     * The current revision ID for which the log is being parsed
     */
    private String revision=null;

    /**
     * The GXLNode corresponding to revision above
     */
    private GXLNode currentNode;

    /**
     * This flag determines if the revision file lists are passed to the GXL node as attributes
     * Set this to false if using DOT as gxl2dot chokes on it
     */
    private boolean includeFiles=false;

    /**
     * Load a file containing a map of author identifiers to colors
     * This file should consist of lines like
     * jcrisp@s-r-s.co.uk=black
     * 
     * @param authorFileName the name of the author file (may be qualified)
     * @throws IOException if there is a problem with the author to color mapping file
     */
    private void loadAuthorFile(String authorFileName) throws IOException {
	Properties authorMap=new Properties();
	InputStream authorMapStream=null;
	try {
	    authorMapStream=new FileInputStream(authorFileName);
	    authorMap.load(authorMapStream);
	    List<String> colorList=new ArrayList<String>(Arrays.asList(colors));
	    for(Object key: authorMap.keySet()) {
		String color=(String)authorMap.get(key);
		if(!colorList.contains(color)) {
		    throw new IOException("Illegal color "+color+" in author color map file");
		}
		authorColorMap.put((String)key,color);
		colorList.remove(color);
	    }
	    colors=colorList.toArray(new String[colorList.size()]);
	}
	finally {
	    if(authorMapStream!=null) authorMapStream.close();
	}
    }

    /**
     * Start parsing the output of a monotone log command and output the corresponding GXL graph
     *
     * @param argv the command line arguments, --authorfile <file> to specify a file mapping authors to colors
     * @throws IOException if there is a read error on the input stream or the input stream runs dry
     * @throws IllegalStateException if the header lines aren't as expected     
     */
    private void Start(String argv[]) throws IOException,IllegalStateException {
	nodes=new HashMap<String,GXLNode>();
	if(argv.length>0) {
	    for(int I=0;I<argv.length;I++) {
		if(argv[I]!=null && argv[I].equals("--authorfile")) {
		    if(I<argv.length-1) loadAuthorFile(argv[I+1]);
		    else {
			System.err.println("Usage: Log2Gxl --authorfile <authorfile>");
			System.exit(-1);
		    }
		}
	    }
	}

	GXLDocument gxlDocument = new GXLDocument();
	graph = new GXLGraph("Monotone Log");
	graph.setAttribute(GXL.EDGEMODE,GXL.DIRECTED);
	// graph.setAttr("rotate",new GXLString("90")); // Hmm. This rotates the coordinate system, not the graph drawing direction
	gxlDocument.getDocumentElement().add(graph);
        source=new LineNumberReader(new InputStreamReader(System.in));
	boolean parsing=true;
	while(parsing) {
	    String line=source.readLine();
	    if(line==null) { 
		parsing=false;
		break;
	    }
	    if(!line.equals("-----------------------------------------------------------------")) {
		throw new IOException(source.getLineNumber()+": Input ["+line+"] doesn't look like output of monotone log");
	    }
	    parseHeader();
	    parseFiles();
	    parseChangeLog();
	    commitNode();
	}
	
	// Write the graph to std out
	gxlDocument.write(System.out);
    }
}
